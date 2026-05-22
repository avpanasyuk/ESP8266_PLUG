/**
 * @author Sasha
 * If you press button for more than 10 seconds MCU resets.
 * LED blinks fast (half a sec) when trying to connect as station, slow when sets up AP,
 * solid when connected
 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include "C_General/MyTime.hpp"
#include "C_ESP/StaticWebServer.hpp"
#include <Arduino.h>
#include <EEPROM.h>

static auto &w = avp::StaticWebServer::s; // just an alias to make code shorter

#define DEBUG_SERIAL Serial

extern "C" {
  int debug_puts(const char *s) {
#ifndef NDEBUG
    avp::HTML_Log::Add(s);
    if(DEBUG_SERIAL) {
      DEBUG_SERIAL.print(s);
      DEBUG_SERIAL.flush();
    }
#endif
    return 0;
  }
}

#include "cse7766.h"

static constexpr int ButtonChkPeriod_ms = 100;
static constexpr int ButtonReset_s = 10;

int relayState;
bool SwitchReset = true; // Flag indicating that the hardware button has been released

// esp8266 pins.
#define ESP8266_GPIO13 13                  // Sonof green LED (LOW == ON).
#define ESP8266_GPIO0 0                    // Sonoff pushbutton (LOW == pressed).
#define ESP8266_GPIO12 12                  // Sonoff relay (HIGH == ON).
const int RELAY = ESP8266_GPIO12;          // Relay switching pin. Relay is pin 12 on the SonOff
static constexpr int LED = ESP8266_GPIO13; // On/off indicator LED. Onboard LED is 13 on Sonoff
const int SWITCH = ESP8266_GPIO0;          // Pushbutton.

static void CheckFor(const char *name, ESP8266WebServer &w, float *pvar) {
  if(w.hasArg(name)) {
    *pvar *= w.arg(name).toFloat();
    w.send(200, "text/plain", String(name) + " is set to " + String(*pvar));
    EEPROM.put(0, ratio);
    EEPROM.commit();
  }
} // CheckFor

// Handle hardware switch activation.
// IF BUTTON IS PUSHED FOR MORE THAN "ButtonReset_s" SECONDS, WiFi CONFIGURATION IS ERASED AND MODULE
// REBOOTED
static void ButtonCheck() {
  // look for new button press
  bool SwitchState = (digitalRead(SWITCH));
  static int NumCyclesButtonIsPressed;

  if(!SwitchState) {
    if(++NumCyclesButtonIsPressed >= ButtonReset_s * 1000 / ButtonChkPeriod_ms) {
      WiFi.disconnect(true);
      // button was pressed long enough
      ESP.reset();
    }
  } else NumCyclesButtonIsPressed = 0;

  // toggle the switch if there's a new button press
  if(!SwitchState && SwitchReset == true) {
    if(relayState == HIGH) {
      digitalWrite(RELAY, relayState = LOW);
    } else {
      digitalWrite(RELAY, relayState = HIGH);
    }

    // Flag that indicates the physical button hasn't been released
    SwitchReset = false;
    // delay(50); // De-bounce interlude. NO, ButtonChkPeriod_ms already debounces
  } else if(SwitchState) {
    // reset flag the physical button release
    SwitchReset = true;
  }
} // ButtonCheck

void setup() {
  digitalWrite(LED, HIGH);
  pinMode(LED, OUTPUT); // turn LED off

  pinMode(SWITCH, INPUT_PULLUP);
  delay(10);

  // Switch relay off.
  digitalWrite(RELAY, relayState = LOW);
  pinMode(RELAY, OUTPUT);

#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.begin(74880);
#endif

  // Setup cse7766 serial.
  Serial.flush();
  // Serial.begin(4800);
  DEBUG_SERIAL.println("Here is the debug output!");

  EEPROM.begin(sizeof(ratio));
  delay(10); // Initialasing EEPROM
  struct ratio_t ratio_from_EEPROM;
  EEPROM.get(0, ratio_from_EEPROM); // read calibration values
  if(ratio_from_EEPROM.C > 0.66 && ratio_from_EEPROM.C < 1.5 && ratio_from_EEPROM.V > 0.66 &&
     ratio_from_EEPROM.V < 1.5 && ratio_from_EEPROM.P > 0.5 && ratio_from_EEPROM.P < 2.0) // values look correct
    ratio = ratio_from_EEPROM;

  auto Opts = avp::StaticWebServer::DefaultOpts();

  Opts.Name = NAME; // NAME should be specified in platformio.ini, so it is in sync with upload_port in espota
  Opts.Version = "6.20";
  Opts.AddUsage =
    F("<li> on</ li><li> off</li>"
      "<li> read - returns <em>\"Voltage Current Power Energy "
      "RelayStatus\"</em></li>"
      "<b>Correction multipliers: </b><br>"
      "<form method='get' action='set'><label>Current: </label><input name='CurrentFactor' length=5><input "
      "type='submit'></form>"
      "<form method='get' action='set'><label>Voltage: </label><input name='VoltageFactor' length=5><input "
      "type='submit'></form>"
      "<form method='get' action='set'><label>Power: </label><input name='PowerFactor' length=5><input "
      "type='submit'></form>");
  avp::StaticWiFi_Conn::LED_pin = LED;
  Opts.status_indication_func_ = avp::StaticWiFi_Conn::Blinken;

  avp::StaticWebServer::begin(Opts);

  // WiFi.setPhyMode(WIFI_PHY_MODE_11G); that's 802.11g mode, we do not do it
  
  avp::HTML_Log::begin();
  debug_puts("debug_puts output here!\n");
  debug_printf("Sketch size:     %u\n", ESP.getSketchSize());
  debug_printf("Free sketch space: %u\n", ESP.getFreeSketchSpace());
  debug_printf("Flash chip size:   %u\n", ESP.getFlashChipRealSize());

  w.on("/on", HTTP_GET, [&]() {
    digitalWrite(RELAY, relayState = HIGH);
    w.send(200, "text/plain", String("Relay is ON"));
  });

  w.on("/off", HTTP_GET, [&]() {
    digitalWrite(RELAY, relayState = LOW);
    w.send(200, "text/plain", String("Relay is OFF"));
  });

  w.on("/set", HTTP_GET, [&]() {
    CheckFor("VoltageFactor", w, &ratio.V);
    CheckFor("CurrentFactor", w, &ratio.C);
    CheckFor("PowerFactor", w, &ratio.P);
    w.send(200, "text/plain", String("Ratio is set to ") + ratio.V + " " + ratio.C + " " + ratio.P);
  });

  w.on("/read", HTTP_GET, [&]() {
    w.send(200, "text/plain", String(voltage) + " " + current + " " + power + " " + energy + " " + relayState + "\n");
  });
} // setup

void loop() {
  // put your main code here, to run repeatedly:
  avp::Periodically<ButtonCheck>::Run(ButtonChkPeriod_ms);
  avp::Periodically<ReadCse7766>::Run(1000);
  avp::StaticWebServer::call_in_loop();
  yield();
} // loop
