---
name: legacy-named-plug-invisible-to-fleet
description: A pre-self-naming plug answers only at its compile-time NAME, so it is absent from Debug_log, the fleet state and the server log; update it by POSTing plug.bin to its /update.
metadata:
  type: project
---

A plug flashed before self-naming (firmware ≤ 6.30) carries its **compile-time `#define NAME`**,
never posts to bsd's `Debug_log.csv`, and never polls the fleet server. So every fleet-side check
reports it as nonexistent: missing from the device-name list, from `.fleet_ota_state.json`, and
from the `http_server` log. **Absence there is not evidence the unit is off — curl the legacy name
directly before concluding it is dead or was never flashed.**

Update it by POSTing the *published* fleet image straight from bsd, with the relay already open
(boot forces the relay LOW, so a live load would be a power cut — see
[[never-cycle-a-plug-to-force-ota]]):

```sh
ssh bsd "curl -m 180 -F 'update=@/POOL/Packages/TEMP/FIRMWARE/plug.bin' http://<LEGACY_NAME>/update"
```

`StaticWebServer` mounts `HTTPUpdateServer` at `/update` with no auth, and it was already there at
6.30. Posting the published binary rather than a local rebuild is what makes the unit's first
fleet poll return 304 instead of re-flashing it — the same reason [[fleet-deploy-not-espota]]
exists. After the reboot the unit renames itself `plug-<last-3-MAC>` and the legacy name stops
resolving.

**How to apply:** when a plug is physically plugged in but no fleet-side record of it exists, probe
the legacy name before believing any of the three fleet-side sources.
