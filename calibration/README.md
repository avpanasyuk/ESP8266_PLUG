# Calibrating the plugs

The compiled-in trim `ratio = {V 1.04, C 1.04, P 1.08}` came from a Kill-A-Watt, and the
Kill-A-Watt is the instrument in error — see `.claude-memory/calibration-trim-is-a-killawatt-artefact.md`
for the evidence and the mechanism. This directory holds what is being done about it.

## The two legs are in very different states

**Voltage — measurable here.** The house has independent witnesses on the same service: the two
pollable UPS units report `input.voltage`, and five plugs report their own. Nothing needs to be
touched physically.

**Current and power — not measurable here.** A shunt-resistance error scales the current register
and the CF power pulses by the same factor, so the plug's own `V·I ≈ P` identity survives it and
cannot detect it. Closing this leg needs an external current or energy reference. The bench has
nothing better than ±0.5 % (`LAB_EQUIP` rule 4), the panel meter's own voltage channel is
uncalibrated, and the only traceable instrument on the property is the utility revenue meter.

## `log_line_voltage.sh` — the voltage-gain regression

A single-point comparison confounds gain with offset, and the trim is a *gain* claim. Service
voltage swings a few volts over a day as house load changes, so sampling across that swing
separates them: regress each plug's volts against a UPS's volts, and the **slope** is the gain
error the `ratio.V` trim purports to correct while the intercept absorbs the offset.

Runs on bsd — always up, on the same service, and the only host that can reach both UPS daemons.
Read-only; it writes no EEPROM and touches no relay.

```sh
scp calibration/log_line_voltage.sh bsd:/POOL/WORK/PLUG/
ssh bsd 'cd /POOL/WORK/PLUG && nohup ./log_line_voltage.sh line_voltage.csv 24 60 </dev/null >run.log 2>&1 &'
```

Arguments are the output CSV, the run length in hours, and the sample period in seconds. Output is
one row per sample: UTC, both UPS voltages, then each plug's five `/read` fields. An unreachable
plug contributes five empty fields rather than shifting the later columns.

**Read the UPS columns before trusting them.** A dead instrument returns a plausible constant, not
an error, and a UPS that reports a fixed voltage is indistinguishable from a quiet line in any
single sample. The run is long enough to show whether each one actually moves; judge that first,
then the regression.

Reduce the finished CSV and commit it here beside the script — it is small, expensive to retake,
and the whole point is to have it later.
