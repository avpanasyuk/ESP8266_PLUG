#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#else
#include <WiFi.h>
#endif

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>         
#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <SimpleTimer.h>
#include "cse7766.h"

#define NAME "plug1"

static constexpr int STR_SIZE = 32;
static constexpr uint32_t SIGNATURE = 102938475;

static struct ip_config_t {
  uint32_t Signature;
  char ssid[STR_SIZE], password[STR_SIZE];
} ip_config;

static const char *ssid = "L";
static const char *password = "group224";

static AsyncWebServer server(80);
static DNSServer dns;

static SimpleTimer timer;

int relayState;  
bool SwitchReset = true;       // Flag indicating that the hardware button has been released

// esp8266 pins.
#define ESP8266_GPIO13 13          // Sonof green LED (LOW == ON).
#define ESP8266_GPIO0 0            // Sonoff pushbutton (LOW == pressed).
#define ESP8266_GPIO12 12          // Sonoff relay (HIGH == ON).
const int RELAY = ESP8266_GPIO12;  // Relay switching pin. Relay is pin 12 on the SonOff
const int LED = ESP8266_GPIO13;    // On/off indicator LED. Onboard LED is 13 on Sonoff
const int SWITCH = ESP8266_GPIO0;  // Pushbutton.

//flag for saving data
static bool shouldSaveConfig = false;

//callback notifying us of the need to save config
static void saveConfigCallback () {
  shouldSaveConfig = true;
} // saveConfigCallback

static void CheckFor(const char *name, AsyncWebServerRequest *request, float *pvar) {
  if (request->hasArg(name)) {
    *pvar *= request->arg(name).toFloat();
    request->send(200, "text/plain", String(name) + " is set to " + String(*pvar));
    EEPROM.put(0, ratio);
    EEPROM.commit();
  }
}  // CheckFor

// Handle hardware switch activation.
void ButtonCheck() {
  // look for new button press
  bool SwitchState = (digitalRead(SWITCH));

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

void setup(void) {
  // Initialize pins.
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(SWITCH, INPUT_PULLUP);
  delay(10);
  // Switch relay off, LED on.
  digitalWrite(RELAY, relayState = LOW);
  
  // Setup cse7766 serial.
  Serial.flush();
  Serial.begin( 4800 );

  // read IP_CONFIG from EEPROM
  EEPROM.get(sizeof(ratio), ip_config);
  if(ip_config.Signature != SIGNATURE) { // nothing stored yet, setting default values
    strcpy(ip_config.ssid, "L");
    strcpy(ip_config.password, "group224");
    ip_config.Signature == SIGNATURE;
  }

  ////// WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  AsyncWiFiManager wifiManager(&server,&dns);
  
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));




WiFi.mode(WIFI_STA);
    WiFi.hostname(NAME);
    WiFi.begin(ssid, password);

    EEPROM.get(0, ratio);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED, LOW);
      delay(100);
      digitalWrite(LED, HIGH);
      delay(100);
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send(200, "text/plain", "Hi! I am \"" NAME "\" " + WiFi.localIP().toString()); 
  });

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?test=ugu
    digitalWrite(RELAY, relayState = HIGH);
   request->send(200, "text/plain", String("Relay is ON"));
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?test=ugu
    digitalWrite(RELAY, relayState = LOW);
    request->send(200, "text/plain", String("Relay is OFF"));
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?test=ugu
    CheckFor("VoltageFactor", request, &ratio.V);
    CheckFor("CurrentFactor", request, &ratio.C);
    CheckFor("PowerFactor", request, &ratio.P);
  });

  server.on("/read", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send(200, "text/plain", String(voltage) + " " + current + " " + 
      power + " " + energy + " " + relayState + "\n"); 
  });


  AsyncElegantOTA.begin(&server);  // Start ElegantOTA
  server.begin();
  
  // Start a timer for checking button presses @ 100ms intervals.
  timer.setInterval(100, ButtonCheck);

  // Start a timer for checking cse7766 power monitor @ 1000ms intervals.
  timer.setInterval(1000, ReadCse7766);

  // Switch LED on to signal initialization complete.
  digitalWrite(LED, LOW);
}  // setup

void loop(void) {
  // put your main code here, to run repeatedly:
  timer.run();
} // loop

