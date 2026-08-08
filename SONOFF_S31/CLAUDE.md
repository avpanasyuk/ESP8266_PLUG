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

Every minute **while its relay is off**, a plug pulls `http://bsd:8000/firmware/plug.bin` and flashes it if the MD5 differs (`CheckFleetOTA` in `main.cpp`). So an espota push to a relay-off unit is **reverted by whatever is on bsd, about ten seconds after the unit reboots** — `avp::Periodically` initialises its deadline at static-init time, so the first `Run()` fires on the first loop pass and the poll lands the moment WiFi comes up. There is no 60-second grace window to race with a `/on`: a bench build **cannot** be tested by espota-ing a relay-off plug (verified 2026-08-08 — `BOOT,fw=6.37` at 13:36:17, `FW_UPDATE,was=6.37` at 13:36:27). Test by publishing, or by an image built with the poll compiled out.

Deploy by publishing the image, and bump `FW_VERSION` in `main.cpp` first so the running build is identifiable:

```
scp .pio/build/espota/firmware.bin bsd:/POOL/Packages/TEMP/FIRMWARE/plug.bin
```

Keep the outgoing image as `plug.bin.<version>` beside it for rollback. espota is the only way to reach a unit whose load is **on**, since it skips the fleet pull — but it is not free: the flash reboots the unit, and `setup()` forces the relay LOW and leaves it there, so the load loses power and stays off until someone sends `/on`. Treat an espota to a live-load plug as a deliberate power cut, subject to the same "no plug's load survives being switched off" rule, and re-close the relay afterwards. The flash lasts only until the relay is next switched off, when the fleet image reclaims it.

## Enrolling a plug that predates the fleet (old `NAME`, no fleet OTA)

A unit still running a pre-fleet build answers under its own baked-in hostname (`plug8.local`, …), never appears in bsd's `Debug_log.csv`, and cannot self-update. Push **the published image, not a fresh local build** — espota-ing a locally built binary lands a different MD5 than `plug.bin`, so the unit re-flashes itself once more at the first relay-off window:

```
scp bsd:/POOL/Packages/TEMP/FIRMWARE/plug.bin <workdir>\plug.bin
python <framework-arduinoespressif8266>\tools\espota.py -i <old-name>.local -p 8266 -f <workdir>\plug.bin -r
```

Check `/read`'s last field (relay state) first — flashing reboots the unit. After the reboot the unit renames itself to `plug-XXYYZZ` (`avp::DeviceName`), so it is reachable only under the new name; confirm enrollment by its `BOOT` row in `/mnt/T/Debug_log.csv` on bsd.

Finding an unenrolled unit: sweep the LAN and match the Sonoff OUI `c4:5b:be` in `arp -a` against the plugs already known from the fleet log.

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
- Debug output: `debug_puts` (defined in `main.cpp`) tees to `HTML_Log` (the device’s `/log` page), to the fleet-wide `Debug_log.csv` on bsd via `avp::FleetServerDebug`, and to `Serial1` only if `DEBUG_SERIAL` is defined. **Not gated on `NDEBUG`** — a release build still reports events (`BOOT`, `FW_UPDATE`, credential / AP-mode / OTA-failure lines) because C_ESP compiles its per-AP scan chatter out under `NDEBUG`. Building *without* `NDEBUG` posts one row per visible AP per 10-minute rescan, so never point a debug build at the fleet log. The tee is suppressed while an OTA runs (ArduinoOTA's `\r`-terminated progress line would ship as junk rows).

## Power metering: watts come from the CF pulse train, not the chip's power register

`/read` returns `Voltage[V] Current[A] Power[W] Energy[Ws] RelayStatus`. Energy accumulates from boot until `/energy_reset`; nothing clears it automatically.

The CSE7766 refreshes its power register only when a CF period completes, and sets the byte-20 "updated" flag on the single frame that follows — one frame out of the ~20 it emits per second. **At low load that register is worthless to a sampling reader**: at 5.9 W a pulse arrives every ~1.8 s, so six of eight `/read`s reported `0.00 0.00` while energy climbed (measured on `plug-E0F4B2`, 2026-08-08). Zeroing on a frame with the flag clear was the bug — clear means "no news", not "zero", so voltage and current now hold their last value and are cleared only by a real cycle-overflow flag (header `0xF8`/`0xF4`).

Power is derived from the pulses instead, which needs no luck and no separate calibration: the chip defines `P = coefP / power_cycle_us` and CF emits one pulse per `power_cycle`, so **one pulse is exactly `coefP · 10⁻⁶` joules** at any load (measured: 10.52 Ws/pulse on `plug-E0F4B2`, 5.095 Ws/pulse on `plug-E23948` — `coefP` is per-chip). Both edges of the averaging window are pulse arrivals, so the count between them is exact; the window floor is 1 s and stretches to the next pulse. While waiting, the reading is clamped by the one-more-pulse-in-flight upper bound (timed from the last pulse), so a load that goes away decays instead of sticking; 30 s without a pulse is below the meter's floor and reads a hard zero for both power and current — with the relay open the chip still offers its own noise floor, ~0.06 A, as a freshly updated current. The tradeoff is deliberate: a load drawing real power under ~0.2 W reports no current either, which would matter only for something almost purely reactive, and none of this fleet's loads is.

Consequence for consumers: `turn_comp_off.sh` polls field 3 for `< 10 W`, and that field is now continuous rather than mostly zero — the threshold still means what it did, but it no longer trips on a stale zero.

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
