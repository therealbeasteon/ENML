#!/usr/bin/env bash
#
# Run a CMake build and report compiler diagnostics as workflow annotations.
#
# A failed build step exits 1 and says nothing that survives outside the raw
# log. Raw logs need repository permissions to download, so a compile error is
# opaque to anyone reviewing without them - which turns a one-line fix into a
# guessing game, and guessing costs a full CI cycle per attempt.
#
# Usage: run-build.sh <report-title> [cmake --build args...]
#
# Example:
#   run-build.sh 'boot-gcc' --preset host-debug --target boot_state_test --parallel 2

set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: run-build.sh <report-title> [cmake --build args...]" >&2
    exit 2
fi

title="$1"
shift

log="$(mktemp)"
status=0
cmake --build "$@" >"$log" 2>&1 || status=$?

# Compiler and linker diagnostics, plus CMake's own failures.
report="$(grep -E 'error:|fatal error:|warning:|undefined reference|ld returned|CMake Error|Error [0-9]+' \
    "$log" | head -60 || true)"

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
        printf '::notice title=Build warnings (%s)::%s\n' "$title" "$encoded"
    else
        printf '::error title=Build failed (%s)::%s\n' "$title" "$encoded"
    fi
fi

{
    echo "### Build — ${title}"
    echo
    echo '```'
    if [ -n "$report" ]; then echo "$report"; else echo '(no diagnostics captured)'; fi
    echo '```'
} >>"${GITHUB_STEP_SUMMARY:-/dev/null}"

cat "$log"
exit "$status"
