#!/usr/bin/env bash
#
# Run a labelled CTest selection and report which tests failed as workflow
# annotations.
#
# CTest exits 8 for "some tests failed" and says nothing else that survives
# outside the raw log. Raw job logs need repository permissions to download, so
# a failure is opaque to anyone reviewing without them - and an opaque failure
# in a suite with a known intermittent test is indistinguishable from a real
# regression. Annotations are attached to the check run and stay reachable, so
# the next occurrence can be identified instead of re-run on faith.
#
# Usage: run-ctest.sh <label-regex> <report-title> [extra ctest args...]
#
# Example:
#   run-ctest.sh '^(m2-broker|m2-runtime)$' 'broker-clang' --test-dir build/x

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: run-ctest.sh <label-regex> <report-title> [ctest args...]" >&2
    exit 2
fi

label="$1"
title="$2"
shift 2

log="$(mktemp)"
status=0
ctest "$@" --output-on-failure -L "$label" >"$log" 2>&1 || status=$?

# Which tests failed, plus the pass/fail tally.
report="$(grep -E '\*\*\*|tests passed|tests failed|The following tests FAILED|^[[:space:]]+[0-9]+ - |^[0-9]+: ' "$log" || true)"

if [ -n "$report" ]; then
    encoded=''
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        # %25 first: escaping the escape character must precede the sequences
        # that introduce it.
        line="${line//%/%25}"
        line="${line//$'\r'/%0D}"
        encoded="${encoded}${line}%0A"
    done <<ANNOTATION
$report
ANNOTATION

    if [ "$status" -eq 0 ]; then
        printf '::notice title=CTest (%s)::%s\n' "$title" "$encoded"
    else
        printf '::error title=CTest failed (%s)::%s\n' "$title" "$encoded"
    fi
fi

{
    echo "### CTest — ${title}"
    echo
    echo '```'
    if [ -n "$report" ]; then echo "$report"; else echo '(no test summary captured)'; fi
    echo '```'
} >>"${GITHUB_STEP_SUMMARY:-/dev/null}"

cat "$log"
exit "$status"
