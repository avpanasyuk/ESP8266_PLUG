---
name: legacy-named-plug-invisible-to-fleet
description: A pre-6.31 plug carries its compile-time NAME, so it is absent from Debug_log/fleet state and must be updated by POSTing plug.bin to its /update.
metadata:
  type: project
---

A plug flashed before self-naming (firmware ≤ 6.30) answers at its **compile-time `#define NAME`**
(`http://PLUG7/read`), never posts to bsd's `Debug_log.csv`, and never polls the fleet server. So
every fleet-side check reports it as nonexistent: it is missing from the device-name list, from
`.fleet_ota_state.json`, and from `/var/log/http_server.log`. **Absence there is not evidence the
unit is off — probe the legacy name directly.**

Update it by POSTing the *published* fleet image straight from bsd, with the relay already open
(boot forces the relay LOW, so a live load would be a power cut — see
[[never-cycle-a-plug-to-force-ota]]):

```sh
ssh bsd "curl -m 180 -F 'update=@/POOL/Packages/TEMP/FIRMWARE/plug.bin' http://<LEGACY_NAME>/update"
```

`StaticWebServer` mounts `HTTPUpdateServer` at `/update` with no auth, and 6.30 already had it.
Posting the published binary rather than a local rebuild is what makes the first fleet poll 304
instead of re-flashing — the same reason [[fleet-deploy-not-espota]] exists. After the reboot the
unit renames itself `plug-<last-3-MAC>` and the legacy name stops resolving.

**How to apply:** when a plug is "in" but no fleet-side record of it exists, curl its legacy name
before concluding it is dead or was never flashed.
