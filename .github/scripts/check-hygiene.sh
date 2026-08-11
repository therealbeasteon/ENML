#!/usr/bin/env bash
# Keeps the tree free of stale, dead and unattached code.
#
# The tree is clean on these axes today - zero TODO, FIXME or HACK markers, no
# orphaned sources. That is exactly when to add the check: a gate written after
# the debt exists starts life failing and gets suppressed, while one written
# while the count is zero only ever has to hold a line.
#
# Two things are checked, both chosen because they are unambiguous. Judgement
# calls - is this comment too long, is this abstraction premature - are review's
# job, not a script's.
#
# Usage: check-hygiene.sh <label>
set -uo pipefail

label="${1:-hygiene}"
report=""
violations=0

note() { report="${report}${1}%0A"; }
fail() { note "FAIL  $1"; violations=$((violations + 1)); }

# 1. Marker comments.
#
# A TODO is a decision deferred inside code that no longer states what it does.
# The project's convention is that unfinished work lives in docs/ACHIEVEMENTS.md
# where it can be prioritised, not in a comment nobody re-reads.
#
# XXX is matched only as a whole word. Every occurrence in this tree is inside
# an mkdtemp template - /tmp/emnl-thing-XXXXXX - and flagging forty false
# positives would train everyone to ignore this check, which is worse than not
# having it.
markers="$(grep -rnE '\b(TODO|FIXME|HACK|XXX)\b' \
    --include='*.cpp' --include='*.hpp' \
    core system tools tests fuzz 2>/dev/null || true)"
marker_count="$(printf '%s' "$markers" | grep -c . || true)"
if [ "$marker_count" -ne 0 ]; then
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        fail "marker comment: ${line%%:*}"
    done <<< "$markers"
else
    note "ok    no TODO/FIXME/HACK/XXX markers"
fi

# 2. Orphaned sources.
#
# A .cpp that no CMakeLists names is code that compiles nowhere, is tested by
# nothing, and is read as if it were live. It is the most misleading kind of
# dead code because it looks exactly like the rest.
orphans=0
for source in $(find core system tools -name '*.cpp' 2>/dev/null | sort); do
    base="$(basename "$source")"
    dir="$(dirname "$source")"
    while [ "$dir" != "." ] && [ ! -f "$dir/CMakeLists.txt" ]; do
        dir="$(dirname "$dir")"
    done
    if [ ! -f "$dir/CMakeLists.txt" ] || ! grep -qF -- "$base" "$dir/CMakeLists.txt"; then
        fail "orphaned source, named by no CMakeLists: $source"
        orphans=$((orphans + 1))
    fi
done
if [ "$orphans" -eq 0 ]; then
    note "ok    every source is named by a CMakeLists"
fi

escaped="${report//\%0A/%0A}"
if [ "$violations" -ne 0 ]; then
    echo "::error title=Hygiene (${label})::${escaped}"
    exit 1
fi
echo "::notice title=Hygiene (${label})::${escaped}"
