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

#define DEBUG_SERIAL Serial1

extern "C" {
  int debug_puts(const char *s) {
#ifdef DEBUG
    if(a != nullptr) a->AddToLog(s);
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
    delay(50); // De-bounce interlude.
  } else if(SwitchState) {
    // reset flag the physical button release
    SwitchReset = true;
  }
} // ButtonCheck

static void Reconnect() {
  // if(relayState == LOW && WiFi.status() != WL_CONNECTED) ESP.restart();
  if(!WiFi.isConnected()) a->reconnect();
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
  if(ratio_from_EEPROM.C > 0.66 && ratio_from_EEPROM.C < 1.5 && ratio_from_EEPROM.V > 0.66 &&
     ratio_from_EEPROM.V < 1.5 && ratio_from_EEPROM.P > 0.5 && ratio_from_EEPROM.P < 2.0) // values look correct
    ratio = ratio_from_EEPROM;

  auto Opts = ESP_board_sync_server::Default();

  Opts.Name = NAME; // NAME should be specified in platformio.ini, so it is in sync with upload_port in espota
  Opts.Version = "4.02";
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
  Opts.status_indication_func_ = ESP_board_sync_server::BlinkerFunc<LED>;

  a = new ESP_board_sync_server(Opts);

  debug_puts("Logging here...");

  auto &w = a->server;

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

  // Switch LED on to signal initialization complete.
  digitalWrite(LED, LOW);
} // setup

void loop() {
  // put your main code here, to run repeatedly:
  avp::Periodically<ButtonCheck>::Run(ButtonChkPeriod_ms);
  avp::Periodically<ReadCse7766>::Run(1000);
  avp::Periodically<Reconnect>::Run(5UL * 60 * 1000); // 5 minutes to give time to reconnect in AP mode
  a->loop();
  yield();
} // loop
