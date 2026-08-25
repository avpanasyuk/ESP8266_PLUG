---
name: fleet-deploy-not-espota
description: Deploying plug firmware means publishing plug.bin on bsd; an espota push to a relay-off plug is reverted about ten seconds after it boots, so a bench build cannot be tested that way.
metadata:
  type: project
---

The fleet image on bsd is authoritative, not whatever you espota'd. A plug polls
`http://bsd:8000/firmware/plug.bin` while its relay is off and re-flashes on any MD5
mismatch, so an espota push to a relay-off unit is overwritten by the published image —
observed 2026-07-20 (a verified 6.33 upload came back up as 6.32) and again 2026-08-08.

**Why:** `CheckFleetOTA` in `main.cpp` calls `avp::PullUpdateFromFleetServer(NAME, FW_VERSION)`,
MD5-gated, guarded on the relay being open (`!RelayIsOn()`, read back from the pin).

**There is no grace window to race.** `avp::Periodically` initialises `NextTime` at static-init,
so the first `Run()` fires on the first loop pass and the poll lands the moment WiFi connects —
about ten seconds after boot, not sixty. Boot always forces the relay LOW, so a freshly espota'd
unit is relay-off exactly when that first poll happens. Measured 2026-08-08 on `plug-E0F4B2`:
`BOOT,fw=6.37` at 13:36:17, `FW_UPDATE,was=6.37` at 13:36:27. Closing the relay by hand afterwards
is too slow, and mDNS resolution alone can eat that budget.

**How to apply:** deploy by staging to a temp name on bsd, verifying the MD5 against the local
build, then `mv` into place (never write `plug.bin` in situ — a plug can poll a half-written
file). Keep the outgoing image as `plug.bin.<version>` for rollback. Bump `FW_VERSION` in
`main.cpp` and **commit before building**, or the published image carries a `-dirty` GIT_REV that
cannot be traced to a commit. To test a change on hardware, publish it — that is the only path;
bsd's `fleet_ota_watchdog.py` (cron, every 5 min) reverts to `plug.bin.good` if no unit posts a
`BOOT,...,md5=` confirm, so a bad image self-heals. Do not try to hurry adoption along — see
[[never-cycle-a-plug-to-force-ota]].
