#!/bin/sh
# Log every mains-voltage witness in the house against every plug, to settle the plugs'
# voltage-channel gain by regression rather than by a single-point comparison.
#
# A one-shot comparison confounds gain with offset. Service voltage swings a few volts
# across a day as house load changes, so sampling across that swing separates them: the
# fitted SLOPE of plug-volts against UPS-volts is the gain error the ratio.V trim claims
# to correct, and the intercept is what a single point cannot see.
#
# Runs on bsd (always up, on the same service, and the only host that can reach both UPS
# daemons). Read-only: nothing here writes EEPROM or touches a relay.
#
# Usage: log_line_voltage.sh <output.csv> [hours] [period_s]

set -u

OUT=${1:?output csv path required}
HOURS=${2:-12}
PERIOD=${3:-30}

UPSC=/usr/local/bin/upsc
UPS1=UPS                            # unit 1, INNO TECH, on bsd itself
UPS2=CPS@RT-AC66U_B1-E810.local     # unit 2, CyberPower CP1285AVR
PLUGS="plug-E0F4B2 plug-E0F4C4 plug-E0F847 plug-E22C8C plug-E23948"

# A UPS that has died into a plausible constant looks exactly like a quiet line, so its
# raw value is logged every sample and judged afterwards on whether it moves at all.
ups_volts() {
	$UPSC "$1" 2>/dev/null | sed -n 's/^input\.voltage: //p'
}

if [ ! -f "$OUT" ]; then
	printf 'utc,ups1_V,ups2_V'
	for p in $PLUGS; do
		printf ',%s_V,%s_A,%s_W,%s_Wh,%s_relay' "$p" "$p" "$p" "$p" "$p"
	done
	printf '\n'
fi >"$OUT"

END=$(( $(date +%s) + HOURS * 3600 ))

while [ "$(date +%s)" -lt "$END" ]; do
	row="$(date -u +%Y-%m-%dT%H:%M:%SZ),$(ups_volts $UPS1),$(ups_volts $UPS2)"
	for p in $PLUGS; do
		# /read is "V A W Wh relay"; an unreachable unit contributes five empty fields
		# rather than shifting every later column.
		r=$(curl -s -m 5 "http://$p/read" | tr -d '\r\n')
		if [ -n "$r" ]; then
			row="$row,$(echo "$r" | tr ' ' ',')"
		else
			row="$row,,,,,"
		fi
	done
	echo "$row" >>"$OUT"
	sleep "$PERIOD"
done
