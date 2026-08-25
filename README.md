# ESP8266_PLUG

Firmware replacement for the SONOFF S31 "smart plug", so it can be controlled over the LAN. Built
with PlatformIO; every library it needs is in `SONOFF_S31/platformio.ini`.

The S31 is a lot of hardware for its price: a mains relay that is easy to reprogram, plus a
CSE7766 voltage / current / power meter.

## One binary serves the whole fleet

`NAME` comes from `netname` in `platformio.ini`, and each unit derives its own identity
`plug-XXYYZZ` from the last three bytes of its MAC at run time — that is its hostname, its mDNS
name, its softAP SSID and its espota target. Nothing is per-unit at compile time.

WiFi credentials are injected from a gitignored `secrets.ini` (`WIFI_DEFAULT_SSID` /
`WIFI_DEFAULT_PASS`); copy `secrets.ini.example`. Leaving them empty strands a freshly flashed
unit in softAP mode.

## Behaviour

If the plug cannot connect to its most recent SSID it opens an access point named after itself
(`plug-XXYYZZ`). Connect to it and browse to `192.168.4.1` — the same home page opens, where the
SSID and password can be set. Credentials are stored in LittleFS and survive a firmware update.

## HTTP API

`http://plug-XXYYZZ/<command>`:

| command | effect |
|---|---|
| *(nothing)* | home page: identity, version, usage, WiFi scan, SSID form, calibration forms |
| `on` / `off` | close / open the relay |
| `read` | `Voltage[V] Current[A] Power[W] Energy[Wh] RelayStatus` |
| `energy_reset` | zero the energy accumulator (nothing else clears it) |
| `set?{Voltage,Current,Power}Factor=x` | multiply that calibration factor by `x` and store it |
| `log` | debug log |
| `update` | upload a firmware image from the browser |
| `reset` | reboot |
| `config?ssid=…&pass=…` | set credentials |

Power is derived from the meter's CF pulse train rather than its power register, so it stays
continuous down to the meter's ~0.2 W floor. Below that a plug reads zero watts *and* zero amps.

Calibration: the plugs come within a couple of percent, and the calibrated defaults are compiled
in. To trim one against a reference instrument, enter the ratio of reference to reported value in
the home page's `Current` / `Voltage` / `Power` fields; the multipliers persist in EEPROM. The
field is a multiplier, not the value — so `1.0` is the no-op. Anything that is not a positive
number, or that would push a factor outside the span `setup()` accepts back from EEPROM, is
refused and named in the reply rather than stored; `set` answers each request exactly once,
listing what it did to every factor named plus the resulting three values.

## Updating a deployed unit

Deploy by publishing a new `plug.bin` on the fleet server — each unit pulls it at its next
relay-off window, so a rollout never cuts a live load. Details, and the exception for a unit
old enough to predate self-naming, are in `.claude-memory/`.

A published image is not a one-way door: `fleet_ota_watchdog.py` (C_ESP) runs on bsd every five
minutes and owns the firmware files. An image a device fetches and then fails to confirm within
the confirm window is moved aside as `plug.bin.bad-<n>`, its md5 blacklisted, the last confirmed
image restored, and mail sent — so a build that cannot boot un-deploys itself. It promotes to
`good_md5` only once a device has confirmed it. Check `.fleet_ota_state.json` beside the image to
see which units confirmed what.
