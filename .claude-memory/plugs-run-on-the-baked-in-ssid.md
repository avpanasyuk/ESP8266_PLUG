---
name: plugs-run-on-the-baked-in-ssid
description: Most of the fleet has no stored LittleFS credentials, so changing WIFI_DEFAULT_SSID/PASS in secrets.ini strands them.
metadata:
  type: project
---

As of 2026-08-26, four of the five responding plugs report `No stored credentials found!`
at boot — only plug-E0F4C4 has a `/net_auth.txt`. The rest connect purely on the
compile-time `WIFI_DEFAULT_SSID` / `WIFI_DEFAULT_PASS` injected from `secrets.ini`.

**Why:** changing those build flags does not just alter a fallback — it is the *only*
credential those units have. Publish a `plug.bin` built against a different default SSID
and every one of them comes up SoftAP-only after its next fleet pull, out of reach of the
fleet server that would have fixed it. Same reason a long press (`ForgetAUTH`, added in
6.42) is a no-op on them: there is nothing stored to erase.

**How to apply:** before touching the WiFi defaults in `secrets.ini`, POST the new
credentials to each unit's `/config?ssid=…&pass=…` first so the LittleFS copy exists,
*then* publish. Read the current state off any unit's `/log` — the WiFi bring-up trace
survives there from 6.42 onward; older firmware wiped it before it could be read.
Deploy is still publish-only, see [[fleet-deploy-not-espota]].
