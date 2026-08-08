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
#include "C_ESP/service.h"            // avp::DeviceName
#include "C_ESP/FleetServerOTA.hpp"   // avp::PullUpdateFromFleetServer
#include "C_ESP/FleetServerDebug.hpp" // avp::FleetServerDebug (debug -> bsd Debug_log.csv)
#include "C_ESP/BootLog.hpp"          // avp::LogBoot
#include <Arduino.h>
#include <EEPROM.h>

#ifndef GIT_REV
#define GIT_REV "nogit" // overridden by git_rev.py extra_script at build time
#endif
// Human deploy counter, bumped on every upload; the single version source.
#define FW_VERSION "6.37"
// Web-page form, FW_VERSION with the build's commit appended. Fleet OTA is
// MD5-gated, so both forms are informational.
static constexpr const char *Version = FW_VERSION "+" GIT_REV;

static auto &w = avp::StaticWebServer::s; // just an alias to make code shorter

// #define DEBUG_SERIAL Serial // idiotic idea: UART0 is both connected to cse7766 and connected to
// header, UART1 is not accessable. Reenable DEBUG_SERIAL is  you want debug output, but
// it kills cse7766 communication

// Teed to the device's own /log page and to the fleet-wide Debug_log.csv on bsd.
// Deliberately NOT gated on NDEBUG: what survives a release build is event-level only
// (BOOT, FW_UPDATE, credential/AP-mode/OTA-failure lines) because C_ESP compiles its
// per-AP scan chatter out under NDEBUG. A build WITHOUT NDEBUG posts one row per AP per
// 10-minute rescan -- do not point a debug build at the fleet log.
extern "C" {
  int debug_puts(const char *s) {
    avp::HTML_Log::Add(s);
    // ArduinoOTA's progress line ends in '\r', which never flushes a row -- it would
    // ship as overlong junk once the 160-char buffer filled. Nothing during an OTA is
    // worth centralizing anyway.
    if(!avp::StaticWiFi_Conn::OTA_IsInProgress) avp::FleetServerDebug::puts(s);
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.print(s);
    DEBUG_SERIAL.flush();
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
  DEBUG_SERIAL.println("Here is the debug output!");
#else
  // Setup cse7766 serial.
  Serial.flush();
  Serial.begin(4800);
#endif


  EEPROM.begin(sizeof(ratio));
  delay(10); // Initialasing EEPROM
  struct ratio_t ratio_from_EEPROM;
  EEPROM.get(0, ratio_from_EEPROM); // read calibration values
  if(ratio_from_EEPROM.C > 0.66 && ratio_from_EEPROM.C < 1.5 && ratio_from_EEPROM.V > 0.66 &&
     ratio_from_EEPROM.V < 1.5 && ratio_from_EEPROM.P > 0.5 && ratio_from_EEPROM.P < 2.0) // values look correct
    ratio = ratio_from_EEPROM;

  auto Opts = avp::StaticWebServer::DefaultOpts();

  // Per-device identity "plug-XXYYZZ" (last 3 MAC bytes) so one common firmware
  // serves the whole fleet: hostname / mDNS / softAP SSID / espota target are all
  // unique per unit. NAME (=${common.netname}=\"plug\") is only the shared fleet
  // prefix + the fleet-OTA base name (<NAME>.bin).
  Opts.Name = avp::DeviceName(NAME);
  Opts.Version = Version;
  // Tee debug output to the fleet-wide log. Opts.Name points at DeviceName's static
  // buffer, so it stays valid for the life of the sink.
  avp::FleetServerDebug::begin("http://bsd:8000/", Opts.Name);
  Opts.AddUsage =
    F("<li> <a href='/on'>on</a></li><li> <a href='/off'>off</a></li>"
      "<li> <a href='/read'>read</a> - returns <em>\"Voltage[V] Current[A] Power[W] Energy[Ws] "
      "RelayStatus\"</em></li>"
      "<li> <a href='/energy_reset' onclick='return confirm(\"Zero the accumulated energy?\")'>energy_reset</a>"
      " - zero the energy accumulator</li>"
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
#ifdef DEBUG // bench-only flash inventory: pure noise in the fleet log
  debug_puts("debug_puts output here!\n");
  debug_printf("Sketch size:     %u\n", ESP.getSketchSize());
  debug_printf("Free sketch space: %u\n", ESP.getFreeSketchSpace());
  debug_printf("Flash chip size:   %u\n", ESP.getFlashChipRealSize());
#endif

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

  w.on("/energy_reset", HTTP_GET, [&]() {
    ResetEnergy();
    w.send(200, "text/plain", String("Energy accumulator zeroed"));
  });
} // setup

// Poll the fleet server for a new <NAME>.bin and flash it if the running image
// differs (MD5-gated; a cheap 304 otherwise, so it is safe every cycle). A
// successful update REBOOTS, and boot forces the relay LOW (see setup) -- so only
// auto-update while the relay is already OFF. That way a fleet rollout never cuts
// power to a live load, and a plug powering a normally-off reserve server updates
// itself precisely in its safe window. No load here tolerates being switched off to
// hurry an update along; a plug whose load is ON waits for its own next off window,
// or takes an espota push to plug-XXYYZZ (which lasts only until that window).
static void CheckFleetOTA() {
  if(relayState == LOW) avp::PullUpdateFromFleetServer(NAME, FW_VERSION);
} // CheckFleetOTA

// The BOOT row has to wait for the connection -- FleetServerDebug silently drops
// anything posted before WL_CONNECTED, so emitting it from setup() would lose it.
static void LogBootOnce() {
  static bool done = false;
  if(done || WiFi.status() != WL_CONNECTED) return;
  done = true;
  // No `extra`: LogBoot reads and reports RSSI itself, so passing one here duplicated
  // the field in the fleet CSV.
  avp::LogBoot(FW_VERSION, GIT_REV);
} // LogBootOnce

void loop() {
  // put your main code here, to run repeatedly:
  avp::Periodically<ButtonCheck>::Run(ButtonChkPeriod_ms);
  // Every pass, not periodically: the meter emits a frame each ~50 ms into a 256-byte
  // UART buffer, and a stale frame costs both pulse timing and update flags.
  ReadCse7766();
  avp::Periodically<LogBootOnce>::Run(1000); // no-op after the first connected pass
  avp::Periodically<CheckFleetOTA>::Run(60UL * 1000); // fleet pull-OTA, once a minute (relay-off only)
  avp::StaticWebServer::call_in_loop();
  yield();
} // loop
