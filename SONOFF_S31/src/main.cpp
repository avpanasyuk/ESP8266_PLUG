/**
 * @author Sasha
 * WiFi configuration is stored in EEPROM, which is simulated in flash really
 * Module tries to connect to stored WiFi first, and go to AP mode if not successful
 * You can connect to "plug?" WiFi network, go to 192.168.4.1 and set WiFi connection 
 * (or control switch).
 * If you press button for more than 10 seconds WiFi configuration is erased.
 * plug?/on and plug?/off control switch, plug?/read reads stuff, just plug?/ is 
 * configuration page
 * LED blinks fast (half a sec) when trying to connect as station, slow when sets up AP,
 * solid when connected
*/
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino
#else
#include <WiFi.h>
#endif

#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SimpleTimer.h>

#include "cse7766.h"

#define NAME "plug5"
#define VERSION 1.3

static constexpr int STR_SIZE = 32;
static constexpr uint32_t SIGNATURE = 102938475;

static struct ip_config_t {
  char ssid[STR_SIZE], password[STR_SIZE];
  uint32_t Signature;
} ip_config;

static const char *ssid = "L";
static const char *password = "group224";
static constexpr int ButtonChkPeriod_ms = 100;
static constexpr int ButtonReset_s = 10;

static AsyncWebServer server(80);

static SimpleTimer timer;

int relayState;
bool SwitchReset = true;  // Flag indicating that the hardware button has been released

// esp8266 pins.
#define ESP8266_GPIO13 13          // Sonof green LED (LOW == ON).
#define ESP8266_GPIO0 0            // Sonoff pushbutton (LOW == pressed).
#define ESP8266_GPIO12 12          // Sonoff relay (HIGH == ON).
const int RELAY = ESP8266_GPIO12;  // Relay switching pin. Relay is pin 12 on the SonOff
const int LED = ESP8266_GPIO13;    // On/off indicator LED. Onboard LED is 13 on Sonoff
const int SWITCH = ESP8266_GPIO0;  // Pushbutton.

static void CheckFor(const char *name, AsyncWebServerRequest *request, float *pvar) {
  if (request->hasArg(name)) {
    *pvar *= request->arg(name).toFloat();
    request->send(200, "text/plain", String(name) + " is set to " + String(*pvar));
    EEPROM.put(0, ratio);
    EEPROM.commit();
  }
}  // CheckFor

// Handle hardware switch activation.
// IF BUTTON IS PUSHED FOR MORE THAN "ButtonReset_s" SECONDS, WiFi CONFIGURATION IS ERASED AND MODULE
// REBOOTED
static void ButtonCheck() {
  // look for new button press
  bool SwitchState = (digitalRead(SWITCH));
  static int NumCyclesButtonIsPressed;

  if(!SwitchState) {
    if(++NumCyclesButtonIsPressed >= ButtonReset_s*1000/ButtonChkPeriod_ms) {
      // button was pressed long enough
      strcpy(ip_config.ssid, "WiFi config was reset!");
      strcpy(ip_config.password, "dummy");
      ip_config.Signature = SIGNATURE;
      EEPROM.put(sizeof(ratio), ip_config);
      EEPROM.commit();
      delay(3000);
      ESP.reset();
    }
  } else NumCyclesButtonIsPressed = 0;

  // toggle the switch if there's a new button press
  if (!SwitchState && SwitchReset == true) {
    if (relayState == HIGH) {
      digitalWrite(RELAY, relayState = LOW);
    } else {
      digitalWrite(RELAY, relayState = HIGH);
    }

    // Flag that indicates the physical button hasn't been released
    SwitchReset = false;
    delay(50);  // De-bounce interlude.
  } else if (SwitchState) {
    // reset flag the physical button release
    SwitchReset = true;
  }
}  // ButtonCheck

static String WiFi_Around;
static void scan() {
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();

  WiFi_Around = "<ol>";
  for (int i = 0; i < n; ++i) {
    // Print SSID and RSSI for each network found
    WiFi_Around += "<li>";
    WiFi_Around += WiFi.SSID(i);
    WiFi_Around += " (";
    WiFi_Around += WiFi.RSSI(i);

    WiFi_Around += ")";
    WiFi_Around += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    WiFi_Around += "</li>";
  }
  WiFi_Around += "</ol>";
}  // scan

static void Reconnect() {
  if(WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED)
      WiFi.begin(ip_config.ssid, ip_config.password);
} // Reconnect

static void AP_mode_LED() {
  static int State = 0;
  if(WiFi.getMode() == WIFI_AP) digitalWrite(LED, State = 1 - State);
  Serial1.printf(".");
} // AP_mode_LED

void setup(void) {
  // Initialize pins.
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(SWITCH, INPUT_PULLUP);
  delay(10);
  // Switch relay off, LED on.
  digitalWrite(RELAY, relayState = LOW);

  // I do not understand what's going on with two serial ports
  // port0 - TX = GPIO1, RX = GPIO3 - THE ONLY PLACE cse7766 can be connected to, as it need RX line
  // It is connected to Serial
  // port1 - TX = GPIO2 - connected to Serial1

  // Setup cse7766 serial.
  Serial.flush();
  Serial.begin(4800);

  // Setup cse7766 serial.
  Serial1.flush();
  Serial1.begin(115200);
  Serial1.printf("Where does it go?\n");

  scan(); // fills WiFi_Around

  EEPROM.begin(sizeof(ratio) + sizeof(ip_config));
  delay(10);  // Initialasing EEPROM
  struct ratio_t ratio_from_EEPROM;
  EEPROM.get(0, ratio_from_EEPROM);  // read calibration values
  if (ratio_from_EEPROM.C > 0.66 && ratio_from_EEPROM.C < 1.5 &&
      ratio_from_EEPROM.V > 0.66 && ratio_from_EEPROM.V < 1.5 &&
      ratio_from_EEPROM.P > 0.5 && ratio_from_EEPROM.P < 2.0)  // values look correct
    ratio = ratio_from_EEPROM;

  // read IP_CONFIG from EEPROM and try to connect
  EEPROM.get(sizeof(ratio), ip_config);
  if (ip_config.Signature != SIGNATURE) {  // nothing stored yet, setting default values
    strcpy(ip_config.ssid, ssid);
    strcpy(ip_config.password, password);
    ip_config.Signature = SIGNATURE;
  }

  // setup Web Server
  static String content;
  static IPAddress ip;

  server.on("/", HTTP_GET, [&](AsyncWebServerRequest *request) {
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    content = String("<!DOCTYPE HTML>\r\n<html>Hello from <b>") + NAME + "</b> at ";
    content += ipStr + ", Version: " + VERSION;
    content += "<p>WiFi networks:</p>";
    content += "<p>";
    content += WiFi_Around;
    content += String("</p><form method='get' action='set'><label>SSID: </label><input name='ssid' length=") + (STR_SIZE - 1) +
               " value='" + ip_config.ssid + "'><input name='pass' length=" + (STR_SIZE - 1) +
               "><input type='submit'></form>";
    content += "Correction multipliers: \r\n";
    content += "<form method='get' action='set'><label>Current: </label><input name='CurrentFactor' length=5><input type='submit'></form>";
    content += "<form method='get' action='set'><label>Voltage: </label><input name='VoltageFactor' length=5><input type='submit'></form>";
    content += "<form method='get' action='set'><label>Power: </label><input name='PowerFactor' length=5><input type='submit'></form>";
    content += "</html>";
    request->send(200, "text/html", content);
  });

  server.on("/scan", [&](AsyncWebServerRequest *request) {
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

    content = "<!DOCTYPE HTML>\r\n<html>go back";
    request->send(200, "text/html", content);
  });

  server.on("/on", HTTP_GET, [&](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?test=ugu
    digitalWrite(RELAY, relayState = HIGH);
    request->send(200, "text/plain", String("Relay is ON"));
  });

  server.on("/off", HTTP_GET, [&](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?test=ugu
    digitalWrite(RELAY, relayState = LOW);
    request->send(200, "text/plain", String("Relay is OFF"));
  });

  server.on("/set", HTTP_GET, [&](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?test=ugu
    CheckFor("VoltageFactor", request, &ratio.V);
    CheckFor("CurrentFactor", request, &ratio.C);
    CheckFor("PowerFactor", request, &ratio.P);

    String qsid = request->arg("ssid");
    String qpass = request->arg("pass");
    if (qsid.length() > 0 && qpass.length() > 0) {
      strcpy(ip_config.ssid, qsid.c_str());
      strcpy(ip_config.password, qpass.c_str());
      Serial1.printf("<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>>>>>");
      EEPROM.put(sizeof(ratio), ip_config);
      EEPROM.commit();
      request->send(200, "text/plain", String("SSID: ") + qsid + ", PWD: " + qpass + ", resetting module...");
      delay(3000);
      ESP.reset();
    }
  });

  server.on("/read", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(voltage) + " " + current + " " + power + " " + energy + " " + relayState + "\n");
  });

  AsyncElegantOTA.begin(&server);  // Start ElegantOTA
  server.begin();

  /////////////////////// IT IS TIME TO CONNECT ! ///////////////
  // trying to connect in station mode
  WiFi.mode(WIFI_STA);
  WiFi.hostname(NAME);
  WiFi.begin(ip_config.ssid, ip_config.password);

  // Wait for connection
  static constexpr int NUM_TRIES = 20;
  for (int TryI = 0; TryI < NUM_TRIES; TryI++) {
    if (WiFi.status() == WL_CONNECTED) break;
    digitalWrite(LED, LOW);
    delay(300);
    digitalWrite(LED, HIGH);
    delay(300);
  }

  if (WiFi.status() != WL_CONNECTED) {  // failed to connect in station mode, let's try AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(NAME, "");
    ip = WiFi.softAPIP();
  } else ip = WiFi.localIP();

  // Start a timer for checking button presses @ 100ms intervals.
  timer.setInterval(ButtonChkPeriod_ms, ButtonCheck);

  // Start a timer for checking cse7766 power monitor @ 1000ms intervals.
  timer.setInterval(1000, ReadCse7766);

  timer.setInterval(5000, Reconnect);
  timer.setInterval(1000, AP_mode_LED);

  // Switch LED on to signal initialization complete.
  digitalWrite(LED, LOW);
}  // setup

void loop(void) {
  // put your main code here, to run repeatedly:
  timer.run();
}  // loop
