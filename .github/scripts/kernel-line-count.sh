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
#   aarch64_kernel_translation_domain.hpp  A reviewed contract for the future
#                   no-stack split-TTBR0/TTBR1 machine transition (M7.7). Its
#                   own comments say so: "construction, sealing and hardware
#                   activation remain separate transactions." Nothing in
#                   cookie_kernel_aarch64_boot includes it yet - only its own
#                   unit test does - so it is not in the shipped image. It
#                   moves into `machine` the moment something in the boot path
#                   includes it, which is the whole point of listing it here
#                   by name rather than letting it slip in unclassified.
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
    "core/oskernel/include/os/kernel/address_space_epoch.hpp"
    "core/oskernel/include/os/kernel/capability.hpp"
    "core/oskernel/include/os/kernel/interrupt.hpp"
    "core/oskernel/include/os/kernel/ipc_continuation.hpp"
    "core/oskernel/include/os/kernel/ipc_endpoint.hpp"
    "core/oskernel/include/os/kernel/ipc_syscall.hpp"
    "core/oskernel/include/os/kernel/kernel.hpp"
    "core/oskernel/include/os/kernel/process_translation.hpp"
    "core/oskernel/include/os/kernel/rendezvous.hpp"
    "core/oskernel/include/os/kernel/scheduler.hpp"
    "core/oskernel/include/os/kernel/scheduler_deadline.hpp"
    "core/oskernel/include/os/kernel/translation_root.hpp"
    "core/oskernel/include/os/kernel/user_access.hpp"
    "core/oskernel/src/abi.cpp"
    "core/oskernel/src/address_space_epoch.cpp"
    "core/oskernel/src/capability.cpp"
    "core/oskernel/src/interrupt.cpp"
    "core/oskernel/src/ipc_continuation.cpp"
    "core/oskernel/src/ipc_endpoint.cpp"
    "core/oskernel/src/ipc_syscall.cpp"
    "core/oskernel/src/kernel.cpp"
    "core/oskernel/src/process_translation.cpp"
    "core/oskernel/src/rendezvous.cpp"
    "core/oskernel/src/scheduler.cpp"
    "core/oskernel/src/scheduler_deadline.cpp"
    "core/oskernel/src/user_access.cpp"
)

machine_files=(
    "core/oskernel/include/os/kernel/aarch64.hpp"
    "core/oskernel/include/os/kernel/aarch64_asid.hpp"
    "core/oskernel/include/os/kernel/aarch64_entry.hpp"
    "core/oskernel/include/os/kernel/aarch64_exception.hpp"
    "core/oskernel/include/os/kernel/aarch64_execution_universe.hpp"
    "core/oskernel/include/os/kernel/aarch64_gic_v3.hpp"
    "core/oskernel/include/os/kernel/aarch64_ipc_syscall.hpp"
    "core/oskernel/include/os/kernel/aarch64_kernel_mapping_manifest.hpp"
    "core/oskernel/include/os/kernel/aarch64_mapping_state.hpp"
    "core/oskernel/include/os/kernel/aarch64_page_tables.hpp"
    "core/oskernel/include/os/kernel/aarch64_preemption.hpp"
    "core/oskernel/include/os/kernel/aarch64_translation.hpp"
    "core/oskernel/include/os/kernel/aarch64_translation_root_sealer.hpp"
    "core/oskernel/include/os/kernel/aarch64_user_access.hpp"
    "core/oskernel/include/os/kernel/aarch64_user_copy_guard.hpp"
    "core/oskernel/include/os/kernel/aarch64_user_frames.hpp"
    "core/oskernel/include/os/kernel/machine.hpp"
    "core/oskernel/include/os/kernel/machine_aarch64.hpp"
    "core/oskernel/src/aarch64_asid.cpp"
    "core/oskernel/src/aarch64_context_switch.S"
    "core/oskernel/src/aarch64_entry.cpp"
    "core/oskernel/src/aarch64_execution_universe.cpp"
    "core/oskernel/src/aarch64_execution_universe_machine.cpp"
    "core/oskernel/src/aarch64_gic_v3.cpp"
    "core/oskernel/src/aarch64_ipc_syscall.cpp"
    "core/oskernel/src/aarch64_preemption.cpp"
    "core/oskernel/src/aarch64_translation.cpp"
    "core/oskernel/src/aarch64_user_access.S"
    "core/oskernel/src/aarch64_user_access.cpp"
    "core/oskernel/src/aarch64_user_copy_guard.cpp"
    "core/oskernel/src/aarch64_user_frames.cpp"
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
    "core/oskernel/include/os/kernel/aarch64_kernel_translation_domain.hpp"
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
# Raised a fourth time, not by a milestone but by a defect fix: discovery
# 1217 -> 1221, total 4453 -> 4457. gic_v3_discovery.cpp rejected the whole
# device tree walk - not just the GIC node, every node - whenever any sibling
# declared #address-cells/#size-cells outside [1,2]. Real QEMU virt always has
# such a sibling (/cpus declares #size-cells = <0>), so this was not a
# hardening gap but a 100%-reproducible silent boot hang: the image built,
# QEMU launched, and the serial log came back completely empty because the
# halt landed before boot_uart was ever assigned. hardware_inventory.cpp
# already carries the fix for this exact class of defect, recorded there as
# the M7.5d lesson; gic_v3_discovery.cpp is a second, independent DTB walker
# added in M7.5g that never inherited it. Splitting the malformed-encoding
# check from the unrepresentable-range check to match that established
# pattern costs 4 lines.
# Raised a fifth time, by M7.5h: core 1280 -> 1360, machine 1545 -> 1766,
# total 4457 -> 4758. This is the milestone that makes the kernel preemptive
# rather than cooperative: deadline scheduling authority (core, alongside the
# scheduler it extends) and, on the AArch64 side, exception-frame decoding for
# a lower-EL context interrupted mid-execution and the actual preemption path
# that switches away from it (machine). A kernel that can enter EL0 and
# deliver a timer IRQ to it (M7.5g) but cannot preempt what it interrupted
# still only runs one process cooperatively; this is what makes M7.5i's
# multi-process scheduling meaningful rather than aspirational.
# Raised a sixth time, by M7.5i: core 1360 -> 1674, machine 1766 -> 2174,
# entry 411 -> 569, total 4758 -> 5638. This is the milestone that makes an
# address space something the kernel can retire and reissue rather than a
# single fixed thing the boot image sets up once: generation-bound epochs and
# process translation as portable policy (core, alongside the scheduler it
# now cooperates with); ASID assignment, execution-universe composition and
# the AArch64 side of committing a translation root under it, plus sealing a
# translation root so it cannot be mutated out from under a running process
# (machine); wiring two real EL0 processes through that machinery at boot,
# which is what proves generation-bound epochs against something other than a
# host test (entry). ASID quarantine exists so a retired generation's
# translations cannot be observed by a later one that reuses its ASID - a
# stale entry surviving a generation boundary is the address-space analogue
# of the stale-TLB-entry defect M7.5e's unmap fixed, and untested here is
# exactly the kind of gap this ceiling exists to keep visible rather than
# quietly under-reviewed.
# Raised a seventh time, not by a milestone but by a defect fix: entry
# 569 -> 570, total 5638 -> 5639. The M7.5i boot proof's contention check
# was flaky under QEMU TCG on shared CI - Scheduler::choose() correctly
# charges all elapsed real time since the last decision even while
# uncontested (the anti-gaming property that stops a thread dodging its
# charge by avoiding decision points, which stays exactly as it is), and
# two EL0/EL1 round trips plus a UART print were measured exceeding 2ms of
# guest-visible time - kernel-internal servicing cost, not the user
# thread's own work, and microseconds on real hardware - exhausting
# process A's round-robin slice before this deliberate contention test
# ever ran. Fixed by passing the still-current since-start() timestamp to
# the contention-detecting reschedule() call instead of a fresh clock
# read, which keeps this decision uncontested-in-effect regardless of
# emulator speed; machine_set_timer() reads the real hardware counter
# internally, so the deadline it arms is still correct relative to actual
# elapsed time. Genuine elapsed time returns for the on_timer() paths,
# which take their timestamp from the delivered interrupt.
# Raised an eighth time, by M7.6a: core 1674 -> 2872, machine 2174 -> 2661,
# entry 570 -> 751, total 5639 -> 7505. This is the milestone that gives
# Cookie its own native IPC instead of borrowing the Linux substrate's
# SOCK_SEQPACKET+SCM_RIGHTS - the transport M8's substrate cutover exists
# to replace. Capability-addressed endpoints, continuations and the reply-
# seal syscall surface are portable policy (core, alongside the scheduler
# and process-translation machinery they compose with); the AArch64 side of
# that same surface plus bounded user-memory copy with an explicit fault
# guard - a syscall that copies from an untrusted, unprivileged-supplied
# pointer without one is the direct kernel analogue of the cache-attack
# class this project already refuses to ship AES with - are machine.
# Reply seals exist so a capability that answered a request cannot be
# replayed to answer a second one it was never granted for, which is worth
# stating beside the number: this is the surface most exposed to
# unprivileged, potentially hostile callers of any code landed so far.
# Raised a ninth time, by M7.7: machine 2661 -> 2757, entry 751 -> 826,
# total 7505 -> 7676. core/oskernel/include/os/kernel/aarch64_kernel_
# translation_domain.hpp - the actual KernelTranslationDomain contract this
# milestone names - is excluded above (see not_kernel) because nothing in
# the boot image includes it yet; what is counted here is the machinery it
# is written against and the boot path it will eventually be spliced into.
# machine gains Stage1Region (lower/upper) on EarlyStage1Builder, so a
# kernel-region root can be distinguished from a process root and refuse
# map_user_page() the way translation_root_errors::wrong_region already
# implies. entry gains named per-stage failure diagnostics (fail(stage))
# replacing bare halt() at boot's roughly forty stop points, so a failure
# reads as which stage rejected the machine rather than as an opaque QEMU
# timeout - the same category of fix M7.5's own halt-site work made once
# already, applied to the sites this milestone's review passed over them.
core_ceiling=2872
machine_ceiling=2757
discovery_ceiling=1221
entry_ceiling=826
total_ceiling=7676

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
