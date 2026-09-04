---
name: calibration-trim-is-a-killawatt-artefact
description: The {1.04, 1.04, 1.08} trim came from a Kill-A-Watt that reads ~4% high; two UPSes agree with the UNTRIMMED plug voltage to 0.2%.
metadata:
  type: project
---

The compiled-in defaults `ratio = {V 1.04, C 1.04, P 1.08}` in `cse7766.cpp` were set by comparing
a plug against a **Kill-A-Watt** the plug was plugged into (he confirmed the reference on
2026-09-04; the source comment says only "they were pretty similar on two first plugs"). **The
evidence says the Kill-A-Watt was the instrument in error, and the trim should not be there.**

**Observation** — three simultaneous samples, 2026-09-04 13:22, five plugs against the two
pollable UPS units (`ssh bsd "upsc UPS"` and `upsc CPS@RT-AC66U_B1-E810.local`, both `input.voltage`):

| | volts |
|---|---|
| UPS 1 (INNO TECH, on bsd) | 122.6 – 122.8 |
| UPS 2 (CyberPower CP1285AVR) | 124.0 |
| plugs **as published** (×1.04) | 127.15 – 129.49, mean 128.6 |
| plugs **÷ 1.04**, i.e. the chip's factory number | 122.3 – 124.5, mean 123.6 |

The untrimmed plugs agree with the two UPSes to **0.2 %**; the published readings sit **4.2 %**
above them. Three independent instrument families (Chipsea CSE7766 factory calibration, INNO TECH,
CyberPower) would all have to be low by the same 4 % for the trim to be right.

**Inference, and the reason it is more than one bad number:** `1.04 × 1.04 = 1.0816 ≈ 1.08`. A
single gain error in a meter's shared voltage reference scales its volts and its amps identically
and its watts **quadratically** — which reproduces all three trim values from one cause. So the
triple is not three independent trims that happen to be wrong; it is one Kill-A-Watt reference
error of ≈ +4 %, seen three times. This also means the current trim carries **no independent
information** about the plug's current path: the Kill-A-Watt's amps error is degenerate with its
volts error, so nothing here validates or condemns `ratio.C`.

**Why the plug's own fields cannot settle the current path either:** for a resistive load
`V·I ≈ P` holds on the plug (measured 0.994 raw on `plug-E23948` at ~720 W), but a shunt-resistance
error scales the current register **and** the CF power pulses by the same factor, so that identity
survives it. Only an external current or energy reference can close it. The house has none better
than ±0.5 % ([[../../../../LAB_EQUIP]] rule 4), and the panel meter's own voltage channel is
uncalibrated, so it is not a witness either.

**How to apply:**
- Treat published watts as **~8 % high** and published volts as **~4 % high** until the trim is
  reverted. Read a unit's live factors with `GET /set` and no arguments.
- Reverting is `GET /set?VoltageFactor=0.961538&CurrentFactor=0.961538&PowerFactor=0.925926`
  (per unit; `plug-E0F847` carries 1.04/1.05/1.09, so its factors differ). **Not done yet** — it
  changes `Ws_per_pulse` for future pulses only, putting an unmarked slope discontinuity in the Wh
  accumulator, and HOUSE_POWER is mid-experiment differencing exactly that field. Coordinate first.
- Don't re-derive any of this from the Kill-A-Watt. See [[open-relay-current-is-a-pulse-artefact]]
  for the other field that misleads.
