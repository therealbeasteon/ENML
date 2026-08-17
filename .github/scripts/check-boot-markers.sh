#!/usr/bin/env bash
#
# Checks a Cookie boot log against the one marker manifest.
#
# Usage: check-boot-markers.sh <label> <log> [--pan | --no-pan]
#
# The manifest is cookie-boot-markers.txt beside this script, and its header
# explains why it exists. The short version: the list used to be written three
# times in m7-kernel.yml with three different subsets, so the EL2 entry path was
# verified against 13 of the 33 proofs the EL1 path was.
#
# PAN is an argument rather than a manifest entry because its presence depends on
# the CPU and not on Cookie: `cortex-a72` lacks FEAT_PAN and reports NO_PAN,
# `-cpu max` has it and reports PAN. One of the two must appear, and which one is
# a property of the machine the run chose.

set -uo pipefail

label="${1:?label required}"
log="${2:?boot log required}"
pan_mode="${3:-}"

manifest="$(dirname "$0")/cookie-boot-markers.txt"

if [ ! -s "$log" ]; then
    echo "::error title=Boot markers (${label})::${log} is empty or missing"
    exit 1
fi
if [ ! -s "$manifest" ]; then
    echo "::error title=Boot markers (${label})::marker manifest ${manifest} is missing"
    exit 1
fi

# A panic is checked first and separately. A run that panicked may still have
# emitted most of its markers, and reporting the missing ones instead of the
# panic buries the cause under its consequences.
if grep -F 'COOKIE:PANIC:' "$log"; then
    echo "::error title=Boot markers (${label})::the kernel panicked; the stage it named is above"
    exit 1
fi

missing=''
found=0
total=0
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    total=$((total + 1))
    if grep -F "$marker" "$log" >/dev/null; then
        found=$((found + 1))
    else
        missing="${missing}${marker}%0A"
    fi
done < "$manifest"

case "$pan_mode" in
    --pan)
        total=$((total + 1))
        if grep -F 'COOKIE:M7.13:PAN' "$log" >/dev/null; then
            found=$((found + 1))
        else
            missing="${missing}COOKIE:M7.13:PAN (this CPU implements FEAT_PAN)%0A"
        fi
        ;;
    --no-pan)
        total=$((total + 1))
        if grep -F 'COOKIE:M7.13:NO_PAN' "$log" >/dev/null; then
            found=$((found + 1))
        else
            missing="${missing}COOKIE:M7.13:NO_PAN (this CPU lacks FEAT_PAN)%0A"
        fi
        ;;
    '') : ;;
    *)
        echo "::error title=Boot markers (${label})::unknown mode ${pan_mode}; expected --pan or --no-pan"
        exit 1
        ;;
esac

if [ -n "$missing" ]; then
    printf '::error title=Boot markers (%s)::%s of %s present. Missing:%%0A%s\n' \
        "$label" "$found" "$total" "$missing"
    exit 1
fi

printf '::notice title=Boot markers (%s)::all %s markers present\n' "$label" "$total"
