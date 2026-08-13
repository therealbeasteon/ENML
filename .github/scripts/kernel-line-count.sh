#!/usr/bin/env bash
# Counts the lines that have to be trusted, and refuses to let the number grow.
#
# docs/M7_0_KERNEL.md rests the whole case for writing a kernel on one claim:
# that it stays small enough for a person to read completely. It names the
# target - QNX at 605 lines for the microkernel, 15,930 for an entire operating
# system - and states the failure condition plainly: "If ENML's kernel reaches
# Linux's size, the exercise has failed regardless of what else it achieved."
#
# Nothing measured that. A ceiling nobody enforces is a wish, and a kernel grows
# the way every kernel grows: one defensible file at a time, each of which is
# fine, none of which is ever the one that crossed the line. This script makes
# the line an artefact of the build rather than of memory.
#
# Usage: kernel-line-count.sh <label>
set -uo pipefail

label="${1:-kernel-line-count}"

# ---------------------------------------------------------------------------
# What counts as "the kernel"
# ---------------------------------------------------------------------------
#
# The boundary is: code that executes with kernel privilege in the shipped
# image. Concretely, that is the source list of the cookie_kernel_aarch64_boot
# target in core/oskernel/CMakeLists.txt, plus the headers those files depend
# on. It is a boundary the build already draws, so it cannot drift from what is
# actually trusted, and a reviewer can check it against one CMake target.
#
# Four categories are counted separately, because a single number would hide
# where the growth is and which part is being compared against 605. Every
# category is trusted; the split is about legibility and about stopping lines
# from being laundered between categories to dodge a ceiling.
#
#   core       The privileged portable runtime: the four responsibilities from
#              M7_0_KERNEL.md - address spaces and threads, synchronous message
#              passing, interrupt dispatch, capability transfer - plus the ABI.
#              This is the number that is comparable to QNX's 605.
#
#   machine    The AArch64 port: context switch, vectors, translation, and the
#              machine.hpp contract the core is written against. Trusted, but
#              architecture-specific and not what "605 lines" described. It is
#              counted and capped separately so that a port cannot quietly
#              absorb work that belongs in the core, or vice versa.
#
#   discovery  Boot-time hardware discovery: FDT parsing, hardware inventory,
#              boot memory planning. The tempting argument is that this is not
#              the trusted runtime - it runs once, before any user process
#              exists, and could one day be a boot service outside the kernel.
#              That argument is rejected here. It runs at EL1 with translation
#              off, it parses a blob supplied by firmware, and a defect in it is
#              a defect in the most privileged code on the machine. It is
#              counted. It gets its own category only so that the day it moves
#              out of the kernel is visible as a drop in one number.
#
#   entry      The image's entry path: reset vector, freestanding memory
#              primitives, and the boot routine that assembles a running system.
#              Privileged by definition.
#
# Excluded, deliberately and by name:
#
#   machine_host.*  A host stand-in for the machine layer, used by the tests. It
#                   is not in the boot image and never runs on a device, so
#                   counting it would inflate the trusted total with code that
#                   is not trusted. It is listed in `not_kernel` below rather
#                   than merely omitted, so that the exclusion is a decision on
#                   the page instead of an accident of a glob.
#
#   CMakeLists.txt, *.ld.in  Build description. Not code that runs.
#
# Any file under core/oskernel that appears in neither a category nor the
# exclusion list fails this check. That is the point: a new kernel source file
# cannot be added without someone deciding, in this file, what it is.

# ---------------------------------------------------------------------------
# What counts as a line
# ---------------------------------------------------------------------------
#
# Blank lines and comments are excluded. Two reasons, and the second is the one
# that matters:
#
#  1. The reference number is a code size. QNX's 605 lines and 7 KB describe an
#     implementation, not a file. Comparing a commented header against it would
#     be comparing different things and would flatter this project.
#
#  2. A ceiling that counts comments taxes explanation. This repository's
#     kernel sources carry long comments explaining why a barrier is where it
#     is - see the -mstrict-align note in core/oskernel/CMakeLists.txt for the
#     kind of thing that gets discovered the hard way and must be written down.
#     A gate that punishes writing that down is a gate that makes the kernel
#     harder to review, which is the exact opposite of what it exists for.
#
# Known limitation: the stripper below is lexical and does not track string
# literals, so a "//" or "/*" inside a string would be mistaken for a comment
# and the line undercounted. Kernel sources here contain no such literal, and a
# stripper that parsed C++ properly would be a larger thing to trust than the
# code it measures.
strip_comments='
BEGIN { inblock = 0; count = 0 }
{
    line = $0; out = ""; i = 1; n = length(line)
    while (i <= n) {
        two = substr(line, i, 2)
        if (inblock) {
            if (two == "*/") { inblock = 0; i += 2 } else { i++ }
        } else if (two == "/*") {
            inblock = 1; i += 2
        } else if (two == "//") {
            break
        } else {
            out = out substr(line, i, 1); i++
        }
    }
    gsub(/[ \t\r]/, "", out)
    if (out != "") count++
}
END { print count + 0 }
'

# ---------------------------------------------------------------------------
# The categories
# ---------------------------------------------------------------------------

core_files=(
    "core/oskernel/include/os/kernel/abi.hpp"
    "core/oskernel/include/os/kernel/capability.hpp"
    "core/oskernel/include/os/kernel/interrupt.hpp"
    "core/oskernel/include/os/kernel/kernel.hpp"
    "core/oskernel/include/os/kernel/rendezvous.hpp"
    "core/oskernel/include/os/kernel/scheduler.hpp"
    "core/oskernel/src/abi.cpp"
    "core/oskernel/src/capability.cpp"
    "core/oskernel/src/interrupt.cpp"
    "core/oskernel/src/kernel.cpp"
    "core/oskernel/src/rendezvous.cpp"
    "core/oskernel/src/scheduler.cpp"
)

machine_files=(
    "core/oskernel/include/os/kernel/aarch64.hpp"
    "core/oskernel/include/os/kernel/aarch64_entry.hpp"
    "core/oskernel/include/os/kernel/aarch64_exception.hpp"
    "core/oskernel/include/os/kernel/aarch64_gic_v3.hpp"
    "core/oskernel/include/os/kernel/aarch64_mapping_state.hpp"
    "core/oskernel/include/os/kernel/aarch64_page_tables.hpp"
    "core/oskernel/include/os/kernel/aarch64_translation.hpp"
    "core/oskernel/include/os/kernel/machine.hpp"
    "core/oskernel/include/os/kernel/machine_aarch64.hpp"
    "core/oskernel/src/aarch64_context_switch.S"
    "core/oskernel/src/aarch64_entry.cpp"
    "core/oskernel/src/aarch64_gic_v3.cpp"
    "core/oskernel/src/aarch64_translation.cpp"
    "core/oskernel/src/aarch64_vectors.S"
    "core/oskernel/src/aarch64_el0.S"
    "core/oskernel/src/machine_aarch64.cpp"
)

discovery_files=(
    "core/oskernel/include/os/kernel/arch_timer_discovery.hpp"
    "core/oskernel/include/os/kernel/boot_memory.hpp"
    "core/oskernel/include/os/kernel/boot_memory_plan.hpp"
    "core/oskernel/include/os/kernel/fdt.hpp"
    "core/oskernel/include/os/kernel/gic_v3_discovery.hpp"
    "core/oskernel/include/os/kernel/hardware_inventory.hpp"
    "core/oskernel/src/arch_timer_discovery.cpp"
    "core/oskernel/src/boot_memory.cpp"
    "core/oskernel/src/boot_memory_plan.cpp"
    "core/oskernel/src/fdt.cpp"
    "core/oskernel/src/gic_v3_discovery.cpp"
    "core/oskernel/src/hardware_inventory.cpp"
)

entry_files=(
    "core/oskernel/boot/aarch64_boot.cpp"
    "core/oskernel/boot/aarch64_start.S"
    "core/oskernel/boot/freestanding_memory.cpp"
)

# Under core/oskernel but not trusted kernel code. Each entry needs a reason in
# the comment block above.
not_kernel=(
    "core/oskernel/CMakeLists.txt"
    "core/oskernel/boot/aarch64_qemu.ld.in"
    "core/oskernel/include/os/kernel/machine_host.hpp"
    "core/oskernel/src/machine_host.cpp"
)

# ---------------------------------------------------------------------------
# The ceilings
# ---------------------------------------------------------------------------
#
# These are the measured values at the commit that introduced this gate, not
# the values the project wants. That is deliberate. Setting the ceiling at 605
# would make this check red on the day it landed and it would be switched off
# within a week; setting it comfortably above the current count would make it
# measure nothing. Set at the measured value it is a ratchet: the kernel is
# free to shrink and free to be rewritten, and it cannot grow without someone
# editing a number here and defending that edit in review.
#
# Raising a ceiling is allowed. It must happen in the same change that adds the
# lines, so the growth and the decision to permit it are one reviewable diff.
#
# Raised once, by M7.5e: machine 1040 -> 1151, total 3395 -> 3506. The 111 lines
# are page-table leaf teardown, verified mapping retirement and the TLB
# invalidation that must accompany them. Unmapping is not optional for a kernel
# that reclaims address space, and a stale TLB entry after an unmap is a
# use-after-free with hardware caching it. This is the machine layer doing the
# job the machine layer exists for, so the growth is accepted rather than
# argued down.
#
# Raised again, by M7.5f: machine 1151 -> 1362, discovery 723 -> 996,
# entry 352 -> 362, total 3506 -> 4000. This is the first milestone that runs
# code outside EL1 at all, and every added line is load-bearing for that:
# EL0 entry/exit assembly and the guarded user stack, W^X ledger and
# guard-page machinery that make handing control to EL0 safe (machine);
# bounded GICv3 topology discovery from the device tree, needed before any
# interrupt can be routed to an EL0 handler in M7.5g/M7.9 (discovery); and
# validating the guarded EL0 context before eret and installing the first
# EL0 program, which closes the boot path that previously parked in wfe
# forever (entry). None of it is discretionary - a kernel that cannot prove
# the context it is about to drop into is safe has no business dropping into
# it.
#
# Raised again, by M7.5g: machine 1362 -> 1545, discovery 996 -> 1217,
# entry 362 -> 411, total 4000 -> 4453. This is the first milestone that
# delivers a real hardware interrupt to EL0 and returns from it: GICv3
# redistributor/CPU-interface programming and the lower-EL IRQ entry/return
# path (machine); bounded DT discovery of the architected timer PPI, needed
# so the kernel knows which interrupt line to arm without trusting a
# hardcoded number (discovery); and wiring interrupt admission and the timer
# into the boot sequence so a return from IRQ has somewhere defined to go
# (entry). A kernel that can enter EL0 but never preempt it cannot schedule
# more than one process, so this is load-bearing for M7.5h.
core_ceiling=1280
machine_ceiling=1545
discovery_ceiling=1217
entry_ceiling=411
total_ceiling=4453

# The aspiration from docs/M7_0_KERNEL.md, for the gap report. This is not a
# ceiling and is not enforced. It is printed on every run so that the distance
# is a thing the project keeps seeing rather than a thing it stops mentioning.
qnx_microkernel=605
qnx_whole_os=15930

# ---------------------------------------------------------------------------

count_lines() {
    # count_lines <file...> - files are known to exist, see the check below
    local total=0 file lines
    for file in "$@"; do
        [ -f "$file" ] || continue
        lines="$(awk "$strip_comments" "$file")"
        total=$((total + lines))
    done
    printf '%s' "$total"
}

report=""
failed=0

note() {
    report="${report}${1}
"
}

# A classified file that has been deleted or renamed leaves a stale entry, and a
# stale entry silently lowers the measured total. Fail rather than notice: an
# undercount is the failure mode this whole gate exists to prevent. This runs
# before counting because count_lines is called in a subshell and cannot report
# anything back except a number.
counted=("${core_files[@]}" "${machine_files[@]}" "${discovery_files[@]}" "${entry_files[@]}")
for file in "${counted[@]}"; do
    if [ ! -f "$file" ]; then
        failed=1
        note "MISSING ${file} - listed in this script but not on disk"
    fi
done

core_lines="$(count_lines "${core_files[@]}")"
machine_lines="$(count_lines "${machine_files[@]}")"
discovery_lines="$(count_lines "${discovery_files[@]}")"
entry_lines="$(count_lines "${entry_files[@]}")"
total_lines=$((core_lines + machine_lines + discovery_lines + entry_lines))

check() {
    # check <name> <lines> <ceiling>
    if [ "$2" -gt "$3" ]; then
        failed=1
        note "$(printf 'OVER  %-10s %5d lines, ceiling %5d (+%d)' "$1" "$2" "$3" "$(($2 - $3))")"
    else
        note "$(printf 'ok    %-10s %5d lines, ceiling %5d (%d spare)' "$1" "$2" "$3" "$(($3 - $2))")"
    fi
}

check "core" "$core_lines" "$core_ceiling"
check "machine" "$machine_lines" "$machine_ceiling"
check "discovery" "$discovery_lines" "$discovery_ceiling"
check "entry" "$entry_lines" "$entry_ceiling"
check "TOTAL" "$total_lines" "$total_ceiling"

# A source file nobody has classified is a source file nobody decided to trust.
tracked=("${counted[@]}" "${not_kernel[@]}")
present="$(find core/oskernel -type f | sed 's#\\#/#g' | sort)"
for file in $present; do
    known=0
    for entry in "${tracked[@]}"; do
        if [ "$file" = "$entry" ]; then known=1; break; fi
    done
    if [ "$known" -eq 0 ]; then
        failed=1
        note "UNCLASSIFIED ${file} - add it to a category in this script, or to not_kernel with a reason"
    fi
done

# The gap, printed every run, pass or fail. Integer tenths rather than a float,
# to keep this free of bc and of locale-dependent printf.
ratio_tenths=$(((core_lines * 10 + qnx_microkernel / 2) / qnx_microkernel))
note "$(printf 'gap   core is %d.%dx the QNX microkernel (%d lines) and must shed %d lines to reach it' \
    $((ratio_tenths / 10)) $((ratio_tenths % 10)) "$qnx_microkernel" "$((core_lines - qnx_microkernel))")"
note "$(printf 'gap   the whole trusted image is %d lines against %d for the whole of QNX - filesystem, drivers and networking included' \
    "$total_lines" "$qnx_whole_os")"

escaped="${report//\%/%25}"
escaped="${escaped//$'\n'/%0A}"

if [ "$failed" -ne 0 ]; then
    echo "::error title=Kernel line count (${label})::${escaped}"
    exit 1
fi

echo "::notice title=Kernel line count (${label})::${escaped}"
