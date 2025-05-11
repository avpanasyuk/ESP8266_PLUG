/**
 * @author Sasha
 * If you press button for more than 10 seconds WiFi configuration is erased.
 * LED blinks fast (half a sec) when trying to connect as station, slow when sets up AP,
 * solid when connected
 */
#include "C_ESP/board_sync_server.h"
#include "C_General/MyTime.hpp"
#include <Arduino.h>
#include <EEPROM.h>

// where to direct debug_ output
static ESP_board_sync_server *a;

extern "C" {
  int debug_puts(const char *s) {
#ifdef DEBUG
    if (a != nullptr) a->AddToLog(s);
#endif
    return 0;
  }
}

#include "cse7766.h"

#define NAME "S31"
#define VERSION 2.02

static constexpr int STR_SIZE = 32;
static constexpr uint32_t SIGNATURE = 102938475;

static constexpr int ButtonChkPeriod_ms = 100;
static constexpr int ButtonReset_s = 10;
int relayState;
bool SwitchReset = true; // Flag indicating that the hardware button has been released

// esp8266 pins.
#define ESP8266_GPIO13 13         // Sonof green LED (LOW == ON).
#define ESP8266_GPIO0 0           // Sonoff pushbutton (LOW == pressed).
#define ESP8266_GPIO12 12         // Sonoff relay (HIGH == ON).
const int RELAY = ESP8266_GPIO12; // Relay switching pin. Relay is pin 12 on the SonOff
const int LED = ESP8266_GPIO13;   // On/off indicator LED. Onboard LED is 13 on Sonoff
const int SWITCH = ESP8266_GPIO0; // Pushbutton.

static void CheckFor(const char *name, ESP8266WebServer &w, float *pvar) {
  if (w.hasArg(name)) {
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

  if (!SwitchState) {
    if (++NumCyclesButtonIsPressed >= ButtonReset_s * 1000 / ButtonChkPeriod_ms) {
      // button was pressed long enough
      ESP.reset();
    }
  } else
    NumCyclesButtonIsPressed = 0;

  // toggle the switch if there's a new button press
  if (!SwitchState && SwitchReset == true) {
    if (relayState == HIGH) {
      digitalWrite(RELAY, relayState = LOW);
    } else {
      digitalWrite(RELAY, relayState = HIGH);
    }

    // Flag that indicates the physical button hasn't been released
    SwitchReset = false;
    delay(50); // De-bounce interlude.
  } else if (SwitchState) {
    // reset flag the physical button release
    SwitchReset = true;
  }
} // ButtonCheck

static void ToggleLED() {
  static int State = 0;
  digitalWrite(LED, State = 1 - State);
} // TogglePin

// static void ReadCse7766() {
//   static CSE7766 cse7766;
//   static bool first = true;
//   if (first) {
//     cse7766.begin();
//     first = false;
//   }
//   cse7766.read();
//   voltage = cse7766.getVoltage() * ratio.V;
//   current = cse7766.getCurrent() * ratio.C;
//   power = cse7766.getPower() * ratio.P;
//   energy += power / 3600.0; // kWh
// } // ReadCse7766

static void Reconnect() {
  if(WiFi.status() != WL_CONNECTED) ESP.restart(); 
} // Reconnect

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(SWITCH, INPUT_PULLUP);
  delay(10);
  // Switch relay off, LED on.
  digitalWrite(RELAY, relayState = LOW);
  pinMode(RELAY, OUTPUT);

  // Setup cse7766 serial.
  Serial.flush();
  Serial.begin(4800);

  EEPROM.begin(sizeof(ratio));
  delay(10); // Initialasing EEPROM
  struct ratio_t ratio_from_EEPROM;
  EEPROM.get(0, ratio_from_EEPROM); // read calibration values
  if (ratio_from_EEPROM.C > 0.66 && ratio_from_EEPROM.C < 1.5 && ratio_from_EEPROM.V > 0.66 &&
      ratio_from_EEPROM.V < 1.5 && ratio_from_EEPROM.P > 0.5 && ratio_from_EEPROM.P < 2.0) // values look correct
    ratio = ratio_from_EEPROM;

  auto Opts = ESP_board_sync_server::Default();

  Opts.Name = "plug2";
  Opts.Version = "2.00";
  Opts.AddUsage = String("<p>Commands: URL as <b>") + Opts.Name + "./<i>Command</i></p>";
  Opts.AddUsage +=
      F("<ul><li> on</ li><li> off</li>"
        "<li> read</b><i> - returns \"Voltage Current Power Energy "
        "RelayStatus\"</i></li></ul>"
        "Correction multipliers: <br>"
        "<form method='get' action='set'><label>Current: </label><input name='CurrentFactor' length=5><input "
        "type='submit'></form>"
        "<form method='get' action='set'><label>Voltage: </label><input name='VoltageFactor' length=5><input "
        "type='submit'></form>"
        "<form method='get' action='set'><label>Power: </label><input name='PowerFactor' length=5><input "
        "type='submit'></form>");
  a = new ESP_board_sync_server(Opts);

  debug_puts("Logging here...");

  auto &w = a->server;

  w.on("/log", HTTP_GET, [&]() { w.send(200, "text/html", a->GetLog()); });

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

  w.begin(); // Start the server

  // Switch LED on to signal initialization complete.
  digitalWrite(LED, LOW);
} // setup

void loop() {
  // put your main code here, to run repeatedly:
  avp::Periodically<ToggleLED>::Run(500);
  avp::Periodically<ButtonCheck>::Run(ButtonChkPeriod_ms);
  avp::Periodically<ReadCse7766>::Run(1000);
  avp::Periodically<Reconnect>::Run(5000);
  avp::Periodically<ToggleLED>::Run(500);
  a->loop();
} // loop
