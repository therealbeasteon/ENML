#!/usr/bin/env bash
#
# Boots a Cookie Kernel image under QEMU and checks it against the one marker
# manifest. For use on a developer machine.
#
# Usage: tools/boot-cookie-qemu.sh <image> [el1|el2|reloc]
#
#   el1    (default) virt, GICv3, virtualization off, cortex-a72 - entry at EL1
#   el2    virt, GICv3, virtualization on, -cpu max - entry at EL2, which is how
#          phone bootloaders actually hand off (docs/M7_13_HARDWARE_NEUTRALITY.md)
#   reloc  loads a raw .bin 4 MiB above its link address, to check the image is
#          genuinely position-independent rather than merely built that way
#
# Why this exists rather than "read the workflow and retype the command": the
# QEMU invocation, the machine flags and the marker set are the definition of
# what "Cookie boots" means. A developer reproducing a CI failure by
# reconstructing that from a YAML file reproduces something slightly different,
# and then disagrees with CI about whether the failure is real. This runs the
# same machine configuration and calls the same checker CI calls.
#
# Building the image still needs an AArch64 toolchain. If you do not have one,
# the `kernel-arm64-native` job uploads `cookie-kernel-boot-image` on every run
# including failures - download that and point this at the ELF inside it.

set -euo pipefail

image="${1:?usage: boot-cookie-qemu.sh <image> [el1|el2|reloc]}"
mode="${2:-el1}"
here="$(cd "$(dirname "$0")/.." && pwd)"

qemu="${COOKIE_QEMU:-}"
if [ -z "$qemu" ]; then
    if command -v qemu-system-aarch64 >/dev/null 2>&1; then
        qemu="qemu-system-aarch64"
    # Windows installs it here and does not put it on PATH.
    elif [ -x "/c/Program Files/qemu/qemu-system-aarch64.exe" ]; then
        qemu="/c/Program Files/qemu/qemu-system-aarch64.exe"
    else
        echo "qemu-system-aarch64 not found; set COOKIE_QEMU to its path" >&2
        exit 2
    fi
fi

test -s "$image"
log="$(mktemp)"

# `timeout` bounds the run because Cookie's boot proof ends by halting rather
# than by exiting: 124 is the expected status, not a failure.
run_qemu() {
    local status=0
    set +e
    timeout 20s "$qemu" "$@" >"$log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
        cat "$log"
        echo "qemu exited $status" >&2
        exit "$status"
    fi
}

case "$mode" in
    el1)
        run_qemu -machine virt,gic-version=3,virtualization=off -cpu cortex-a72 \
            -m 512M -nographic -monitor none -no-reboot \
            -device loader,file="$image",cpu-num=0
        pan=--no-pan
        ;;
    el2)
        run_qemu -machine virt,gic-version=3,virtualization=on -cpu max \
            -m 512M -nographic -monitor none -no-reboot \
            -device loader,file="$image",cpu-num=0
        pan=--pan
        ;;
    reloc)
        # A raw .bin placed 4 MiB above where it was linked. Every other run
        # loads the ELF where it was linked and therefore cannot test the claim.
        run_qemu -machine virt,gic-version=3,virtualization=off -cpu cortex-a72 \
            -m 512M -nographic -monitor none -no-reboot \
            -device loader,file="$image",addr=0x40600000,force-raw=on \
            -device loader,addr=0x40600000,cpu-num=0
        pan=--no-pan
        ;;
    *)
        echo "unknown mode '$mode'; expected el1, el2 or reloc" >&2
        exit 2
        ;;
esac

cat "$log"
echo "--- markers ---"
"$here/.github/scripts/check-boot-markers.sh" "local-$mode" "$log" "$pan"
