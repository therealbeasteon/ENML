#!/usr/bin/env bash
# Verifies that the hardening flags actually reached the produced binary.
#
# Feature-testing a flag proves the compiler accepted it, not that the
# mitigation is present in the artifact. A typo in a generator expression, a
# target that links the wrong interface library, or a toolchain that quietly
# ignores a linker option all produce a green build and an unhardened binary.
# This reads the ELF and fails if a mitigation is missing.
#
# Usage: check-hardening.sh <label> <binary> [binary...]
set -uo pipefail

label="${1:?label required}"
shift
if [ "$#" -eq 0 ]; then
    echo "::error title=hardening ${label}::no binaries given to check"
    exit 1
fi

report=""
failed=0

note() {
    report="${report}${1}
"
}

fail() {
    failed=1
    note "FAIL  $1"
}

pass() {
    note "ok    $1"
}

for binary in "$@"; do
    if [ ! -f "$binary" ]; then
        fail "$(basename "$binary"): not found at $binary"
        continue
    fi

    name="$(basename "$binary")"
    headers="$(readelf -lWh "$binary" 2>/dev/null)"
    dynamic="$(readelf -dW "$binary" 2>/dev/null)"
    symbols="$(readelf -sW "$binary" 2>/dev/null)"

    # Position independence: a fixed-address image defeats layout randomisation
    # no matter what the loader supports.
    if printf '%s' "$headers" | grep -qE '^ *Type: *DYN'; then
        pass "$name: position independent (DYN)"
    else
        fail "$name: not position independent"
    fi

    # Full RELRO needs both the segment and BIND_NOW; the segment alone leaves
    # the GOT writable, which is the thing being protected.
    if printf '%s' "$headers" | grep -q 'GNU_RELRO'; then
        pass "$name: GNU_RELRO segment present"
    else
        fail "$name: no GNU_RELRO segment"
    fi

    if printf '%s' "$dynamic" | grep -qE 'BIND_NOW|FLAGS_1.*NOW'; then
        pass "$name: BIND_NOW (full RELRO)"
    else
        fail "$name: BIND_NOW absent, RELRO is partial"
    fi

    # An executable stack makes injected data directly runnable.
    stack_line="$(printf '%s' "$headers" | grep 'GNU_STACK' || true)"
    if [ -z "$stack_line" ]; then
        fail "$name: no GNU_STACK entry, stack permissions unknown"
    elif printf '%s' "$stack_line" | grep -qE 'RWE'; then
        fail "$name: stack is executable"
    else
        pass "$name: non-executable stack"
    fi

    # Stack canaries. The reference is only emitted for functions the
    # protector actually instrumented, so its absence in a trivial binary
    # would be a false alarm - which is why this runs against real services.
    if printf '%s' "$symbols" | grep -q '__stack_chk_fail'; then
        pass "$name: stack protector instrumented"
    else
        fail "$name: no __stack_chk_fail reference, stack protector not applied"
    fi
done

# One annotation carrying the whole report: GitHub caps how many it will show,
# and a truncated report drops exactly the failures worth reading. Escape the
# percent signs first, or the newline escapes introduced next are themselves
# re-escaped.
escaped="${report//\%/%25}"
escaped="${escaped//$'\n'/%0A}"

if [ "$failed" -ne 0 ]; then
    echo "::error title=hardening ${label}::${escaped}"
    exit 1
fi

echo "::notice title=hardening ${label}::${escaped}"
