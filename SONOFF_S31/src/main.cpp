#include <Arduino.h>
#include <AsyncElegantOTA.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <SimpleTimer.h>
#include "cse7766.h"

#define NAME "plug2"
const char *ssid = "L";
const char *password = "group224";

AsyncWebServer server(80);
static SimpleTimer timer;

int relayState = LOW;  // Blynk app pushbutton status.
bool SwitchReset = true;       // Flag indicating that the hardware button has been released

// esp8266 pins.
#define ESP8266_GPIO13 13          // Sonof green LED (LOW == ON).
#define ESP8266_GPIO0 0            // Sonoff pushbutton (LOW == pressed).
#define ESP8266_GPIO12 12          // Sonoff relay (HIGH == ON).
const int RELAY = ESP8266_GPIO12;  // Relay switching pin. Relay is pin 12 on the SonOff
const int LED = ESP8266_GPIO13;    // On/off indicator LED. Onboard LED is 13 on Sonoff
const int SWITCH = ESP8266_GPIO0;  // Pushbutton.

static void CheckFor(const char *name, AsyncWebServerRequest *request, float *pvar) {
  if (request->hasArg(name)) {
    *pvar = request->arg(name).toFloat();
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
    if (relayState) {
      digitalWrite(RELAY, LOW);
      relayState = false;
    } else {
      digitalWrite(RELAY, HIGH);
      relayState = true;
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
  digitalWrite(RELAY, LOW);
  
  // Setup cse7766 serial.
  Serial.flush();
  Serial.begin( 4800 );

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

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?test=ugu
    if (request->hasArg("on")) {
      digitalWrite(RELAY, HIGH);
      relayState = true;
      request->send(200, "text/plain", String("Relay is on"));
    }
    if (request->hasArg("off")) {
      digitalWrite(RELAY, LOW);
      relayState = false;
      request->send(200, "text/plain", String("Relay is OFF"));
    }
    CheckFor("VoltageFactor", request, &ratio.V);
    CheckFor("CurrentFactor", request, &ratio.C);
    CheckFor("PowerFactor", request, &ratio.P);
  });

  server.on("/read", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send(200, "text/plain", String(voltage) + ", " + current + ", " + power + ", " + energy); 
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

