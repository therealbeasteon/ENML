#!/usr/bin/env bash
#
# Run every fuzz target briefly, with leak detection on, and report failures as
# workflow annotations.
#
# Two defects motivated this. Leak detection was disabled across the board, so a
# leak in a parser reachable from untrusted input - a denial-of-service surface,
# not untidiness - could not be seen. And targets added after the job was
# written were built but never executed, so a harness could rot without anything
# noticing.
#
# Listing the targets in one place means adding a fuzz target and forgetting to
# smoke it is now a single omission rather than two.
#
# Usage: run-fuzz-smoke.sh <build-dir> <target>=<runs> [<target>=<runs> ...]

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: run-fuzz-smoke.sh <build-dir> <target>=<runs> ..." >&2
    exit 2
fi

build_dir="$1"
shift

# Leak detection on. libFuzzer's own -runs mode exits cleanly, so a report here
# is the harness or the code under test, not the fuzzer.
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

overall=0
summary=''

for spec in "$@"; do
    target="${spec%%=*}"
    runs="${spec##*=}"
    binary="${build_dir}/fuzz/${target}"

    if [ ! -x "$binary" ]; then
        summary="${summary}${target}: MISSING (${binary} not built)"$'\n'
        overall=1
        continue
    fi

    log="$(mktemp)"
    status=0
    "$binary" -runs="$runs" >"$log" 2>&1 || status=$?

    if [ "$status" -eq 0 ]; then
        summary="${summary}${target}: ok (${runs} runs)"$'\n'
    else
        overall=1
        summary="${summary}${target}: FAILED (exit ${status})"$'\n'
        # The lines that identify a leak, a sanitizer report, or a crash.
        while IFS= read -r line; do
            summary="${summary}    ${line}"$'\n'
        done < <(grep -E 'ERROR|SUMMARY|LeakSanitizer|runtime error|Direct leak|Indirect leak|#[0-9]+ 0x' "$log" | head -25 || true)
    fi
    rm -f "$log"
done

encoded=''
while IFS= read -r line; do
    [ -n "$line" ] || continue
    line="${line//%/%25}"
    line="${line//$'\r'/%0D}"
    encoded="${encoded}${line}%0A"
done <<ANNOTATION
$summary
ANNOTATION

if [ "$overall" -eq 0 ]; then
    printf '::notice title=Fuzz smoke::%s\n' "$encoded"
else
    printf '::error title=Fuzz smoke failed::%s\n' "$encoded"
fi

{
    echo '### Fuzz smoke'
    echo
    echo '```'
    echo "$summary"
    echo '```'
} >>"${GITHUB_STEP_SUMMARY:-/dev/null}"

echo "$summary"
exit "$overall"
