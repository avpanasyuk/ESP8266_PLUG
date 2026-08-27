---
name: open-relay-current-is-a-pulse-artefact
description: A plug's amps field reads ~0.06 A for 30 s after each CF pulse, so a sub-floor trickle looks like a steady current.
metadata:
  type: project
---

`/read` field 2 (amps) is **not** a steady reading at sub-floor load. `cse7766.cpp` zeroes
current only after `IdleZero_ms` = 30 s with no CF pulse, and every new pulse restarts that
timer — which lets the chip's ~0.06 A noise floor stand again until the next 30 s elapses.
An outlet passing a trickle of roughly one pulse per minute therefore alternates
`0.00 / 0.06 / 0.00 / 0.06` with no load present.

Measured 2026-08-26, 7 reads at 30 s over 3 min, firmware 6.42, relay OPEN on all three:

| plug | amps sequence | Wh |
|---|---|---|
| plug-E0F4C4 | 0.00 0.05 0.00 0.06 0.00 0.00 0.06 | 0.3896 → 0.3944, in +0.0016 steps |
| plug-E0F847 | 0.00 0.00 0.07 0.00 0.00 0.09 0.00 | 0.3883 → 0.3916, in +0.0016 steps |
| plug-E23948 | 0.00 (then switched on: 1.49, 0.63…) | 2.0895 → 6.3952 once running |

**Why:** +0.0016 Wh is exactly one CF pulse. The amps field rises in lockstep with those
pulses, so
this is the firmware behaving as specified, not a defect. It also means a short burst of
reads is the wrong instrument: five reads inside 10 s all land in one post-pulse window and
report a confident "steady 0.07 A" that does not exist. Reading 5× only defeats noise when
the reads span longer than the thing's own period — here, >30 s.

**How to apply:** to judge whether an outlet is passing anything, watch **field 4 (Wh)** over
minutes, not field 2 over seconds. Never treat "~0.07 A intermittent" as a signature of any
particular downstream condition — it is the meter's floor under any sub-0.2 W trickle. And
check field 5 (relay) first: with the relay open the meter sees nothing downstream at all
(see [[plugs-run-on-the-baked-in-ssid]] for the other field that misleads, and
[[fleet-deploy-not-espota]] for why field 4 resets on a deploy).

**The pulse size is per-unit and calibratable — do not hard-code 0.0016.**
`Ws_per_pulse = ratio.P * coefP / 1e6`, and `ratio.P` is the EEPROM-stored power multiplier
that `/set?PowerFactor=` multiplies, bounded to [0.5, 2.0]. So a recalibration can move the
quantum by nearly 2x, and it already differs across the fleet: on 2026-08-26 `ratio.P` was
1.08 on E0F4B2 / E0F4C4 / E22C8C / E23948 and **1.09 on E0F847**. Read a unit's ratios with
**`GET /set` carrying no arguments** — every Nudge returns false, nothing is written to
EEPROM, and the handler still replies `Ratio is now <V> <C> <P>`. Rules should say "one pulse
step", never a literal Wh number.

**Cross-project dependency:** X570's `x570-wake-needs-real-ac-gap.md` and
`measure-before-switching-power.md` (commit `ce25580`) encode `IdleZero_ms = 30 s` and this
pulse quantum as the basis of their rocker/liveness discriminator. If either changes, message
the x570 session — they asked for that ping explicitly and their notes go stale silently
without it.
