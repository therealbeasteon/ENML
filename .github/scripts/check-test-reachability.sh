#!/usr/bin/env bash
#
# Fails if a registered test is selected by no workflow.
#
# This project has now been bitten twice by the same shape, and the second time
# is why this script exists rather than another note in a document.
#
# Phase 0 (docs/ROADMAP.md): `recovery_policy_test` was registered with CTest
# and absent from a workflow's build-target list, so it reported `Not Run` and
# an absence read as a pass. It blocked a five-PR stack for days.
#
# 2026-08-17: `image_ckx_test` - the gate on the .ckx executable format, the
# thing every Cookie program will be - declared the label `m7-image`, which no
# workflow selected with -L and no workflow's -R pattern matched. It was
# compiled on every push and executed on none of them, from the day it landed.
#
# The lesson both share is the one docs/M7_13 and the emulator-leniency work
# state in another register: **a mechanism that is present is not a mechanism
# that is engaged.** A test suite's coverage is not what it contains, it is
# what something runs. Nothing in CMake or in GitHub Actions relates the two,
# so the relation is checked here.
#
# Method, and the reason it is not a regex reimplementation: CTest's own
# selectors decide what runs, so this asks CTest. Every -L and -R pattern found
# in the workflows is handed back to `ctest -N` in a configured build tree, and
# the union of what they select is compared against the full test list. No
# selector semantics are reimplemented here, so this cannot disagree with CI
# about what a pattern means.
#
# Usage: check-test-reachability.sh <label> <configured-build-dir>

set -uo pipefail

label="${1:?label required}"
build_dir="${2:?configured build directory required}"

# Both directories, because a workflow is not the only place a selector lives:
# run-budgets.sh carries `-L '^budget$'` itself and the workflow only names the
# script. Scanning workflows alone would report every budget test as
# unreachable, which is the failure mode a gate must not have on its first run.
selector_sources=(".github/workflows" ".github/scripts")

if [ ! -d "$build_dir" ]; then
    echo "::error title=test reachability ${label}::${build_dir} is not a configured build tree"
    exit 1
fi

# -E (exclusion) is deliberately ignored.
#
# A job may exclude tests it cannot run - ci.yml's AArch64 job excludes the
# supervisor tests under QEMU user mode - while another job runs them. Since
# this builds a *union* over every workflow, ignoring exclusions can only make
# the check more permissive, never produce a failure that is not real. A test
# excluded everywhere it is selected would still slip through; that is a
# narrower hole than the one being closed and is recorded rather than hidden.
# Comment lines are stripped before extraction, and that is not tidiness.
# Both YAML and shell comment with `#`, so a selector *mentioned* in prose -
# including the one four paragraphs above - would otherwise be collected as a
# selector that runs. That direction of error is the dangerous one: it makes a
# test look reached by a job that does not exist, which is the exact illusion
# this gate exists to remove.
# This file is excluded from its own scan for the same reason: the extraction
# expressions below are themselves text of the form being extracted, so
# including it would collect fragments of this script as selectors.
selectors() {
    # selectors <flag-regex>
    grep -rhE --exclude="$(basename "$0")" -- "$1" "${selector_sources[@]}" |
        grep -vE '^[[:space:]]*#' |
        grep -oE -- "$1"
}

mapfile -t label_patterns < <(
    selectors "-L '[^']+'" | sed -E "s/^-L '//; s/'$//" | sort -u
)
mapfile -t name_patterns < <(
    selectors "-R '[^']+'" | sed -E "s/^-R '//; s/'$//" | sort -u
)
# run-ctest.sh takes its label regex as the first positional argument rather
# than after -L, so it needs its own extraction. A selector this script cannot
# see is a selector whose tests look unreachable, which would be a false
# failure - the noisiest possible way for a gate to be wrong.
mapfile -t script_patterns < <(
    selectors "run-ctest\.sh '[^']+'" |
        sed -E "s/^run-ctest\.sh '//; s/'$//" | sort -u
)
label_patterns+=("${script_patterns[@]}")

if [ "${#label_patterns[@]}" -eq 0 ] && [ "${#name_patterns[@]}" -eq 0 ]; then
    echo "::error title=test reachability ${label}::no ctest selectors found in ${selector_sources[*]}"
    exit 1
fi

# `ctest -N` names tests one per line, and the exact shape varies by CTest
# version - "  Test #1: name" and "  1: name" are both in the wild. Both are
# accepted rather than guessed at, because getting it wrong produces an empty
# list, and an empty list is indistinguishable from "everything is reachable"
# in one direction and "nothing is registered" in the other.
#
# stderr is kept rather than discarded. The first version of this dropped it,
# and when the parse produced nothing the gate could say only "registers no
# tests" - which is exactly the opaque failure `run-ctest.sh`'s own header was
# written to complain about. A gate that cannot say why it failed costs a CI
# round trip per guess.
list_tests() {
    ctest --test-dir "$build_dir" -N "$@" 2>&1 |
        sed -nE 's/^[[:space:]]*(Test[[:space:]]*)?#?[0-9]+: (.+)$/\2/p'
}

all_tests="$(list_tests | sort -u)"
if [ -z "$all_tests" ]; then
    raw="$(ctest --test-dir "$build_dir" -N 2>&1 | head -n 20)"
    encoded=''
    while IFS= read -r line; do
        line="${line//%/%25}"
        encoded="${encoded}${line}%0A"
    done <<< "$raw"
    printf '::error title=test reachability (%s)::no tests parsed from %s. ctest said:%%0A%s\n' \
        "$label" "$build_dir" "$encoded"
    exit 1
fi

selected=""
for pattern in "${label_patterns[@]}"; do
    selected="${selected}$(list_tests -L "$pattern")
"
done
for pattern in "${name_patterns[@]}"; do
    selected="${selected}$(list_tests -R "$pattern")
"
done
selected="$(printf '%s' "$selected" | sed '/^$/d' | sort -u)"

unreachable="$(comm -23 <(printf '%s\n' "$all_tests") <(printf '%s\n' "$selected"))"

total="$(printf '%s\n' "$all_tests" | wc -l | tr -d ' ')"
reached="$(printf '%s\n' "$selected" | sed '/^$/d' | wc -l | tr -d ' ')"

if [ -n "$unreachable" ]; then
    encoded=""
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        encoded="${encoded}${line}%0A"
    done <<< "$unreachable"
    printf '::error title=Unreachable tests (%s)::%s registered, %s selected by a workflow. These run nowhere:%%0A%s\n' \
        "$label" "$total" "$reached" "$encoded"
    printf 'A test no workflow selects is not coverage. Give it a label a\n'
    printf 'workflow already selects, or add a selector for it in\n'
    printf '.github/workflows, in the same commit that registers the test.\n'
    exit 1
fi

printf '::notice title=Test reachability (%s)::%s registered, all selected by at least one workflow\n' \
    "$label" "$total"
