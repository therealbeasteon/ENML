#!/usr/bin/env bash
#
# Run the resource budget gates and publish the measured numbers.
#
# The measurements are the point of the gate, not a debugging aid. Left only in
# the raw log, the sole observable signal is pass/fail, and a ceiling can then
# sit far above real usage indefinitely without anyone noticing. The gate would
# catch a catastrophic regression and miss a creeping one.
#
# Results go to two places:
#   - the job summary, which is where a reviewer reads them;
#   - workflow annotations, which are attached to the check run and remain
#     reachable without permission to download raw logs.
#
# Usage: run-budgets.sh <architecture-label>

set -euo pipefail

architecture="${1:-unknown}"
log="$(mktemp)"

# -V rather than --output-on-failure: a passing run must still print its
# numbers. CTest has no "show output only on success" mode.
status=0
ctest --preset host-debug -V -L '^budget$' >"$log" 2>&1 || status=$?

# The harness's own report lines, plus whatever CTest says about failures.
report="$(grep -E \
    'resource budget:|resident_kib|ready_ms|idle_wakeups_per_sec|OVER BUDGET|skip:|Failed|\*\*\*|tests passed|tests failed|fatal|failed to start|did not reach|did not stay|could not sample|no compiled budget' \
    "$log" || true)"

# One annotation carrying the whole report, not one per line.
#
# GitHub caps how many annotations it surfaces per check run, and a
# line-per-annotation scheme silently drops the tail - which cost us the keys
# service's ready_ms and idle wakeups on its first measured run. Newlines are
# encoded as %0A so a single annotation keeps the full table.
emit_annotations() {
    local level="$1"
    [ -n "$report" ] || return 0

    local encoded=''
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        # %25 first: escaping the escape character must happen before the
        # sequences that introduce it.
        line="${line//%/%25}"
        line="${line//$'\r'/%0D}"
        encoded="${encoded}${line}%0A"
    done <<ANNOTATIONS
$report
ANNOTATIONS

    printf '::%s title=Resource budgets (%s)::%s\n' "$level" "$architecture" "$encoded"
}

summary="${GITHUB_STEP_SUMMARY:-/dev/null}"
{
    echo "### Resource budgets — ${architecture}"
    echo
    echo '```'
    if [ -n "$report" ]; then
        echo "$report"
    else
        echo '(no budget measurements captured)'
    fi
    echo '```'
    echo
    if [ "$status" -eq 0 ]; then
        echo "All budgets within ceiling."
    else
        echo "**A budget gate failed.** If a ceiling was exceeded and the"
        echo "regression is intended, change it in"
        echo "\`tests/budget/include/budget/budget.hpp\` in the same commit so the"
        echo "cost is reviewed rather than absorbed."
    fi
} >>"$summary"

if [ "$status" -eq 0 ]; then
    emit_annotations notice
else
    emit_annotations error
fi

# Always echo the full log to stdout so the raw output stays self-contained.
cat "$log"
exit "$status"
