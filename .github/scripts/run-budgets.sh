#!/usr/bin/env bash
#
# Run the resource budget gates and publish the measured numbers to the job
# summary.
#
# The measurements are the point of the gate, not a debugging aid. Leaving them
# buried in the raw log means the only observable signal is pass/fail, and a
# ceiling can then sit far above actual usage indefinitely without anyone
# noticing. Surfacing them on the run page is what makes tightening a ceiling a
# thing someone actually does.
#
# Usage: run-budgets.sh <architecture-label>

set -euo pipefail

architecture="${1:-unknown}"
log="$(mktemp)"

# -V rather than --output-on-failure: a passing run must still print its
# numbers. CTest has no "show output on success only" mode.
status=0
ctest --preset host-debug -V -L '^budget$' >"$log" 2>&1 || status=$?

summary="${GITHUB_STEP_SUMMARY:-/dev/null}"
{
    echo "### Resource budgets — ${architecture}"
    echo
    echo '```'
    # Extract the harness's own report lines. Anything else in a verbose CTest
    # log is noise for this purpose.
    grep -E 'resource budget:|resident_kib|ready_ms|idle_wakeups_per_sec|OVER BUDGET|skip:' "$log" \
        || echo '(no budget measurements captured)'
    echo '```'
    echo
    if [ "$status" -eq 0 ]; then
        echo "All budgets within ceiling."
    else
        echo "**A budget was exceeded.** If the regression is intended, change the"
        echo "ceiling in \`tests/budget/include/budget/budget.hpp\` in the same commit"
        echo "so the cost is reviewed rather than absorbed."
    fi
} >>"$summary"

# Always echo to stdout too, so the raw log remains self-contained.
cat "$log"
exit "$status"
