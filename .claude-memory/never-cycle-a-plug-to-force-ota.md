---
name: never-cycle-a-plug-to-force-ota
description: Never switch a plug off to make it take a fleet OTA — no plug's load tolerates even ~30 s without power.
metadata:
  type: feedback
---

Do not turn a plug's relay off to force a firmware update, and do not offer it as an
option. No load on any plug in this fleet can be without power for ~30 s.

**Why:** fleet pull-OTA only runs while `relayState == LOW` (`main.cpp`), so a plug
whose relay is ON simply will not update. The tempting move is to `GET /off`, let it
pull, then `GET /on` — that cuts power to whatever it feeds, which the user has ruled
out for every unit.

**How to apply:** publish the image to bsd and stop there. Each unit takes it in its
own natural relay-off window, whenever that happens to be — that is precisely what the
relay-state guard exists for, not an obstacle to work around. If a specific unit must
be updated on demand and its load is ON, espota is the only path (fleet OTA is skipped
while ON), and that flash survives only until the relay is next switched off. See
[[fleet-deploy-not-espota]].
