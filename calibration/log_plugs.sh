#!/bin/sh
# Poll every plug, and the house's mains-voltage witnesses, into one CSV.
#
# Serves two analyses at once:
#
#   * Relay-edge steps. When a plug's relay changes state, whole-house power steps by that
#     plug's load and every other load is unchanged across the step, so it cancels. Compared
#     against the panel meter's mains channel that is one equation in one unknown, which is
#     what the plug's current path needs. HOUSE_POWER measured the estimator: median of the
#     per-edge steps over ONE minute either side (longer windows are monotonically worse --
#     the house random-walks with tau ~ 1 min), robust scale 76 W, so N ~ 30 edges gives 2 %
#     on a 700 W step. The plug's own watts in those same windows are the x variable, which
#     is why the full /read is logged and not just the relay bit.
#   * Voltage gain. Regressing a plug's volts against a UPS's volts across the day's service
#     swing gives a slope, and the slope is what the ratio.V trim actually claims.
#
# PERIOD is 10 s because the panel meter bins at one minute: the edge must be located well
# inside a bin, or the straddling minute has to be dropped on both sides and one window's
# worth of noise is lost per edge. The UPS units are read every UPS_EVERY-th sample only --
# their columns are empty otherwise, and one of them quantises to 1 V anyway.
#
# Read-only with respect to the fleet: no EEPROM write, no relay change.
#
# Usage: log_plugs.sh <output.csv> [period_s] [ups_every_n]
#   Appends if the file exists, so a restart never costs history. Run under `daemon -r` to
#   get restarted on exit; that survives a crash but not a reboot of the host.

set -u

OUT=${1:?output csv path required}
PERIOD=${2:-10}
UPS_EVERY=${3:-6}

UPSC=/usr/local/bin/upsc
UPS1=UPS                            # unit 1, INNO TECH, on bsd itself
UPS2=CPS@RT-AC66U_B1-E810.local     # unit 2, CyberPower CP1285AVR; quantises to 1 V
PLUGS="plug-E0F4B2 plug-E0F4C4 plug-E0F847 plug-E22C8C plug-E23948"

# A UPS that has died reports a plausible constant, never an error, so the raw value is
# logged every time and judged afterwards on whether it moves at all.
ups_volts() {
	$UPSC "$1" 2>/dev/null | sed -n 's/^input\.voltage: //p'
}

# The header is written ONLY when the file is new. It must be a group with its own
# redirection: putting the redirect on the `if` truncates the file whether or not the
# branch runs, which silently empties the log on every restart.
if [ ! -f "$OUT" ]; then
	{
		printf 'utc,ups1_V,ups2_V'
		for p in $PLUGS; do
			printf ',%s_V,%s_A,%s_W,%s_Wh,%s_relay' "$p" "$p" "$p" "$p" "$p"
		done
		printf '\n'
	} >"$OUT"
fi

n=0
next=$(date +%s)

while :; do
	if [ $(( n % UPS_EVERY )) -eq 0 ]; then
		row="$(date -u +%Y-%m-%dT%H:%M:%SZ),$(ups_volts "$UPS1"),$(ups_volts "$UPS2")"
	else
		row="$(date -u +%Y-%m-%dT%H:%M:%SZ),,"
	fi

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
	n=$(( n + 1 ))

	# Schedule against a fixed grid, not against sleep+work, so the sample period does not
	# drift by the time the five reads take.
	next=$(( next + PERIOD ))
	now=$(date +%s)
	if [ "$next" -gt "$now" ]; then
		sleep $(( next - now ))
	else
		next=$now
	fi
done
