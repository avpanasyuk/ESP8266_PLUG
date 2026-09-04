# Calibrating the plugs

The compiled-in trim `ratio = {V 1.04, C 1.04, P 1.08}` came from a Kill-A-Watt, and the
Kill-A-Watt is the instrument in error — see `.claude-memory/calibration-trim-is-a-killawatt-artefact.md`
for the evidence and the mechanism. **The trims are deliberately left in place**: a factor whose
error can be named is worth more than a corrected number of uncertain provenance, and reverting
would make every historical reading unreconstructable. Divide it out; do not remove it.

## The two legs, and why only one of them is easy

**Voltage — settled locally.** The two pollable UPS units report `input.voltage` on the same
service. Regress a plug's volts against a UPS's volts across the day's swing: the **slope** is
what `ratio.V` claims, and a single-point comparison could not have separated it from an offset.

**Current and power — needs an outside reference.** A shunt-resistance error scales the current
register and the CF power pulses by the same factor, so the plug's own `V·I ≈ P` identity survives
it. The bench has nothing better than ±0.5 % (`LAB_EQUIP` rule 4) and the panel meter's own
voltage channel is uncalibrated.

The route that works is **differential**, via HOUSE_POWER's panel meter:

1. **`bill_net + PV_production`** anchors the meter's mains gain — two traceable instruments, the
   utility revenue meter and the inverter's own production meter. ⛔ **Never the bill alone.** The
   house has a **line-side tapped** solar array (producing since 2018, ~12.9 MWh/year), connected
   upstream of the main panel, so the panel's feeder CTs measure **gross** consumption while the
   bill is **net**. Gross is nearly flat year-round at 1450–2150 kWh/month; billed net swings 13×
   across the year purely from production. Anchoring on the bill alone in a spring month would put
   the mains coefficient out by up to 8×, and the plug gain with it.
   Measured by HOUSE_POWER over the 7 billing periods with >50 % meter coverage:
   `EPM_mains / (bill_net + PV_production)` = **0.907**, sd 0.041 — the feeders read ~9 % low.
2. At a plug's relay edge, whole-house power steps by that plug's load while every other load is
   unchanged across the step, so it cancels. Against an anchored mains channel that is one
   equation in one unknown. This stage is **differential and never touches an absolute total**,
   so nothing about the solar affects it.

Caveat carried by step 1: it anchors the *ensemble* gain over the four feeder channels, so per-CT
variation between them stays unconstrained and lands in the residual.

⚠ The array is invisible to every test run from inside the house. A diurnal-shape argument
(midday mains exceeding night mains in every month) appears to rule PV out and does not — a
line-side tap gives gross consumption its normal midday-high shape and it never reverses. Nor
does a hunt for negative feeder power: these CTs are non-directional, so export returns as
positive watts. Both tests were run here and both came back clean on an array that had been
running for eight years. The only evidence that settles it is the inverter's own production data.

## `log_plugs.sh` — the collector for both

Runs on bsd: always up, on the same service, and the only host that reaches both UPS daemons.
Read-only with respect to the fleet — no EEPROM write, no relay change.

```sh
scp calibration/log_plugs.sh bsd:/POOL/WORK/PLUG/
ssh bsd 'cd /POOL/WORK/PLUG && daemon -r -f -p log_plugs.pid ./log_plugs.sh plug_power.csv 10 6'
```

Arguments are the output CSV, the sample period in seconds, and how often to also read the UPS
units (every *n*-th sample). One row per sample: UTC, both UPS voltages, then each plug's five
`/read` fields. An unreachable plug contributes five empty fields rather than shifting the later
columns.

**10 s, not 60 s** — the panel meter bins at one minute, so the edge has to be located well inside
a bin or the straddling minute must be discarded on both sides, costing a window's worth of noise
per edge. The plugs take it comfortably: `/read` is trivial, the web server is single-connection
so 10 s intervals never overlap, and the ~100 ms it blocks `loop()` is well inside the CSE7766's
~500 ms UART buffer headroom.

**The estimator is measured, not assumed** (HOUSE_POWER, on the mains' current epoch): use the
**median** of the per-edge steps over **one minute either side**. Longer windows are monotonically
worse — the house's own load random-walks with τ ≈ 1 min, so a wider window admits more background
drift than it removes noise (robust scale 76 W at 1 min, 184 W at 10 min). At σ_eff = 76 W a 700 W
step needs **N ≈ 30 edges for 2 %**; with a plain mean it would need 625, so the estimator matters
more than the edge count. Drop edges where the mains step disagrees with the plug's own reported
step by more than ~3× the robust scale.

## Two traps in this data

- **`daemon -r` restarts the script if it exits, and the script appends.** The header is written
  only when the file does not exist, inside its own redirected group — putting the redirect on the
  `if` truncates the file whether or not the branch runs, which silently empties the whole log on
  every restart. Re-read that block before editing it — the truncation is silent and costs the
  whole run.
- **A dead instrument reports a plausible constant, never an error.** UPS 2 looked frozen at
  exactly 124.0 across four samples. It is not — it quantises to 1 V and does move (124 → 120 over
  thirteen minutes). Judge that from a long run, never from a handful of samples.
