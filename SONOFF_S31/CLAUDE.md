# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

PlatformIO firmware for a Sonoff S31 smart plug (ESP8266 + CSE7766 power monitor). The plug exposes a small HTTP API (`/on`, `/off`, `/read`, `/set`) over WiFi and supports OTA updates.

## Build / upload / monitor

PlatformIO drives everything. `[common] netname` (= `plug`) is the **fleet prefix**, not a device name: it feeds `-DNAME=` and names the fleet image `<netname>.bin`. Each unit's addressable name is `avp::DeviceName(NAME)` = `plug-XXYYZZ` (last 3 MAC bytes), derived at runtime — that is the mDNS/softAP/espota target, so `--upload-port` must name the individual unit.

```
pio run                       # build default env (espota = release + OTA upload)
pio run -t upload --upload-port plug-XXYYZZ.local   # OTA to one unit
pio run -e esp12e -t upload   # serial upload (first flash, or when OTA is dead); port set by upload_port in platformio.ini
pio run -e debug -t upload    # debug build (-Og -ggdb3 -DDEBUG=1) over OTA path’s upload settings
pio device monitor            # serial monitor at 74880 baud (DEBUG_SERIAL = Serial1)
```

## Deploying: the fleet image is authoritative, espota is not

Every minute **while its relay is off**, a plug pulls `http://bsd:8000/firmware/plug.bin` and flashes it if the MD5 differs (`CheckFleetOTA` in `main.cpp`). So an espota push to a relay-off unit is **reverted within 60 s** by whatever is on bsd — deploy by publishing the image instead, and bump `Version` in `main.cpp` first so the running build is identifiable:

```
scp .pio/build/espota/firmware.bin bsd:/POOL/Packages/TEMP/FIRMWARE/plug.bin
```

Keep the outgoing image as `plug.bin.<version>` beside it for rollback. espota is the right tool only for a unit whose load is **on** (fleet OTA is skipped then, so power is never cut to a live load) — and that flash lasts only until the relay is next switched off.

## Source layout

`build_src_filter` in `platformio.ini` is explicit — only listed files compile. The three `src/C_*` subdirectories are **git submodules** (pinned via `.gitmodules`), tracking the shared libraries' `development` trunk:

- `src/C_General/` — portable C++ utilities (`MyTime`, `Periodically`, `Error`, `HTML_Log`-adjacent helpers, `Macros`). Provides `.clang-format` (symlinked to repo root).
- `src/C_ESP/` — ESP8266/ESP32 glue: `StaticWiFi_Conn` (connect-then-AP-fallback with credentials in LittleFS at `/net_auth.txt`), `StaticWebServer` (sync `ESP8266WebServer` + `HTTPUpdateServer` for OTA HTTP, mDNS, status LED via `hw_timer`), `HTML_Log` (in-RAM ring buffer rendered as the device’s status page), `fast_gpio` (templated direct-register pin ops).
- `src/C_ARDUINO/` — Arduino-platform shims used by both ESP variants.

Project-specific code is just `src/main.cpp` + `src/cse7766.cpp` + `include/cse7766.h`. Anything else lives in the shared libraries — when fixing something cross-cutting, the fix usually belongs in the relevant `C_*` repo and should be committed there too.

## Runtime architecture

- `setup()` initializes EEPROM (stores calibration `ratio` struct with sanity range check), then `avp::StaticWebServer::begin(Opts)`. `StaticWebServer` is a singleton: `avp::StaticWebServer::s` is the underlying `WebServer` instance; `main.cpp` aliases it as `w` and registers route handlers on it.
- `loop()` is just three `avp::Periodically<Fn>::Run(ms)` invocations (button check 100ms, CSE7766 read 1s, web-server tick every iteration) plus `yield()`. No FreeRTOS tasks.
- `StaticWiFi_Conn` runs the status LED from a hardware timer (`Opts.status_indication_func_ = avp::StaticWiFi_Conn::Blinken<LED>`), so the indication keeps working even when `loop()` is blocked. `AVP_RAM_ATTR` is `IRAM_ATTR` (see `platformio.ini`) — anything called from the timer ISR must live in IRAM.
- Debug output: `debug_puts` (defined in `main.cpp`) tees to `Serial1` and to `HTML_Log`, which is what the device’s built-in web log page renders. Compiles to a no-op under `-DNDEBUG`.

## Hardware pins (Sonoff S31)

- GPIO0 — pushbutton (LOW = pressed). Long-press ≥10s wipes WiFi credentials (`WiFi.disconnect(true)`) and resets — this is the only recovery path if AP-mode fallback also fails.
- GPIO12 — relay (HIGH = ON).
- GPIO13 — onboard green LED (LOW = ON). Blink semantics: fast = connecting STA, slow = AP mode, solid = connected.
- `Serial` (UART0) is wired to the CSE7766 at 4800 baud — do not `Serial.print` for debug. Use `Serial1` (TX-only on GPIO2) at 74880 baud or `debug_puts`/`debug_printf`.

## Build flags worth knowing

- `-std=gnu++17`, `-DNO_STL=1` — C_General avoids `<vector>` etc. on MCU targets; prefer the in-repo `Array`/`Vector`/`CircBuffer` headers.
- `-DDO_OTA=1` — gates ArduinoOTA inclusion in `StaticWiFi_Conn`.
- `-DAVP_RAM_ATTR=IRAM_ATTR` — see ISR note above.
- `-mtext-section-literals` — required because some templated code emits literals that the linker otherwise refuses in IRAM sections.

## Code style

`.clang-format` (symlink → `src/C_General/.clang-format`) is LLVM-based with `ColumnLimit: 0`, 2-space indent, attached braces, `AllowShortIfStatementsOnASingleLine: AllIfsAndElse`, `SortIncludes: Never`. Matching the existing style matters because the shared libraries are formatted to it.
