---
name: fleet-deploy-not-espota
description: Deploying plug firmware means publishing plug.bin on bsd; an espota push to a relay-off plug is reverted within 60 s.
metadata:
  type: project
---

The fleet image on bsd is authoritative, not whatever you espota'd. A plug polls
`http://bsd:8000/firmware/plug.bin` once a minute while its relay is off and re-flashes
on any MD5 mismatch, so an espota push to a relay-off unit is overwritten by the older
published image within 60 s — observed on 2026-07-20: a verified 6.33 upload came back
up reporting 6.32.

**Why:** `CheckFleetOTA` in `main.cpp` calls `avp::PullUpdateFromFleetServer(NAME, Version)`
every 60 s, MD5-gated, guarded on `relayState == LOW`.

**How to apply:** deploy by staging to a temp name on bsd, verifying the MD5 against the
local build, then `mv` into place (never write `plug.bin` in situ — a plug can poll a
half-written file). Keep the outgoing image as `plug.bin.<version>` for rollback. Bump
`Version` in `main.cpp` first so a running unit is identifiable. Do not try to hurry
adoption along — see [[never-cycle-a-plug-to-force-ota]].
