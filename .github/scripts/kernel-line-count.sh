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
BEGIN { inblock = 0; count = 0; semis = 0 }
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
    semis += gsub(/;/, ";", out)
    gsub(/[ \t\r]/, "", out)
    if (out != "") count++
}
END { print count + 0, semis + 0 }
'

# ---------------------------------------------------------------------------
# The categories
# ---------------------------------------------------------------------------

core_files=(
    "core/oskernel/include/os/kernel/abi.hpp"
    "core/oskernel/include/os/kernel/address_space_epoch.hpp"
    "core/oskernel/include/os/kernel/capability.hpp"
    "core/oskernel/include/os/kernel/execution_authority.hpp"
    "core/oskernel/include/os/kernel/interrupt.hpp"
    "core/oskernel/include/os/kernel/interrupt_delivery.hpp"
    "core/oskernel/include/os/kernel/ipc_continuation.hpp"
    "core/oskernel/include/os/kernel/ipc_endpoint.hpp"
    "core/oskernel/include/os/kernel/ipc_syscall.hpp"
    "core/oskernel/include/os/kernel/address_space_syscall.hpp"
    "core/oskernel/include/os/kernel/fault_region.hpp"
    "core/oskernel/include/os/kernel/memory_grant.hpp"
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
    "core/oskernel/src/interrupt_delivery.cpp"
    "core/oskernel/src/ipc_continuation.cpp"
    "core/oskernel/src/ipc_endpoint.cpp"
    "core/oskernel/src/ipc_syscall.cpp"
    "core/oskernel/src/address_space_syscall.cpp"
    "core/oskernel/src/fault_region.cpp"
    "core/oskernel/src/memory_grant.cpp"
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
    "core/oskernel/include/os/kernel/aarch64_interrupt_syscall.hpp"
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
    "core/oskernel/src/aarch64_interrupt_syscall.cpp"
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
# Raised a tenth time, by M7.8: core 2872 -> 3052, machine 2757 -> 2772,
# discovery 1221 -> 1234, entry 826 -> 838, total 7676 -> 7896. This closes
# the gap M7.6a's own IPC surface opened: a capability minted for a numeric
# ThreadId is holder-checked by that same bare number, and a recycled
# thread ID - the ordinary result of a process exiting and a new one being
# admitted - would let the new process silently inherit whatever the old
# one could still reach if nothing else changed. execution_authority.hpp
# (core, pulled into the trusted image transitively through capability.hpp
# and process_translation.hpp, both already counted) binds a capability's
# holder to ExecutionAuthority - thread plus AddressSpaceEpoch identity,
# not thread alone - so a recycled ASID with a fresh generation is a
# different holder even at the same ThreadId, and stale capability
# authority does not survive the recycling standing set up to prevent it.
# The migration is deliberately fail-closed: bound capabilities are
# unusable through the legacy ThreadId-only IPC path rather than silently
# losing their generation binding, until M7.8.2 adds the explicit
# ExecutionAuthority IPC path.
# Raised an eleventh time, by M7.9's first increment: core 3052 -> 3117,
# total 7896 -> 7961. interrupt_attach/interrupt_detach/interrupt_complete
# are capability-checked at the Kernel composition layer, closing the gap
# docs/M6_0_DEVICE_ACCESS.md and docs/M7_1_INTERRUPT.md both named and
# deferred: "no interrupt authority... needs its own threat model" and
# "attaching the interrupt capability to the source" respectively.
# InterruptTable itself is unchanged and still does not consult
# CapabilityTable - the check lives here, matching where ipc_send/
# ipc_receive already check IPC capabilities, and re-validates fresh on
# every call rather than caching, so a capability revoked between attach
# and complete cannot still authorize either.
# Raised a twelfth time, by M7.9's second increment: entry 838 -> 867,
# total 7961 -> 7990. cookie_kernel_syscall_entry now decodes and dispatches
# interrupt_attach/interrupt_detach/interrupt_complete, closing gap 3 of the
# four docs/M7_9_USER_SPACE_DRIVERS.md named: "interrupt_attach, interrupt_
# detach and interrupt_complete are reserved in the ABI and unreachable from
# EL0." The three calls route to the capability-checked Kernel:: methods the
# first increment added, fail closed on any Result error exactly as the
# existing send/receive/reply dispatch already does, and return
# interrupt_complete's must-service-again answer in x0 the way a completed
# receive already returns its byte count there. No caller exercises this path
# yet - the driver process that will is the remaining gap, tracked separately
# - so this diff is reviewable on its own terms: decode and dispatch, matching
# an already-tested Kernel:: composition, nothing more.
# Raised a thirteenth time, by M7.9's third increment: machine 2,772 -> 2,790,
# discovery 1,234 -> 1,272, entry 867 -> 892, total 7,990 -> 8,071. This closes
# the second of the four gaps docs/M7_9_USER_SPACE_DRIVERS.md named:
# machine-layer routing for a real, non-timer GICv3 device source.
# discover_architected_timer now also decodes the same DTB node's virtual-
# timer entry (discovery) - reused as M7.9's stand-in device source rather
# than inventing a UART or virtio-mmio driver just to prove the capability
# path, per the design doc's own recorded answer to "which device source the
# first proof uses." initialize_gic_v3_primary_cpu configures both PPIs
# identically but leaves the device PPI disabled until something attaches to
# it, and gic_v3_set_ppi_masked gives the syscall entry a way to move it
# between enabled and disabled at that and every later transition (machine).
# cookie_aarch64_irq_dispatch gains a second branch, structurally parallel to
# the timer's, that routes the device PPI through Kernel::dispatch_interrupt()
# instead of PreemptionCoordinator and masks unconditionally afterward -
# InterruptTable has no slot to ask for a spurious assertion, and leaving an
# asserting line enabled with nobody to charge it to is a livelock at the
# controller regardless of whose fault it is (entry); the same masking
# discipline extends to the interrupt_attach/detach/complete syscall handlers
# PR #74 landed, which decoded these calls but did not yet touch GIC state.
# No caller exercises any of this yet - the driver process that will is M7.9's
# last gap.
# Raised a fourteenth time, closing a gap review found rather than a milestone
# adding a feature: core 3,117 -> 3,223, machine 2,790 -> 2,821, entry
# 892 -> 896, total 8,071 -> 8,212. docs/M7_1_INTERRUPT.md and
# docs/M7_9_USER_SPACE_DRIVERS.md both say begin_service is deliberately not a
# syscall because "the count rides back on the wakeup" - but nothing called
# begin_service or delivered its result anywhere in the tree, which means
# interrupt_complete could never have succeeded outside a test that skips
# straight to it: nothing ever moved a source from pending to in_service.
# interrupt_delivery.hpp/.cpp add InterruptDeliveryTable, a one-slot-per-
# thread handoff mirroring IpcContinuationTable's own shape; dispatch_interrupt
# now calls begin_service itself the instant a driver is woken and arms the
# result there (core). aarch64_interrupt_syscall.hpp/.cpp add
# complete_interrupt_current, the interrupt analogue of complete_ipc_current,
# writing the delivery into x2/x3 on every resume - not x0/x1, which
# complete_ipc_current already uses and a driver that also does IPC would
# otherwise collide with (machine). complete_after_switch in aarch64_boot.cpp
# now calls both completions on every switch (entry). Found and closed before
# M7.9's own boot proof tried to use interrupt_complete and could not have.
# Raised a fifteenth time, by M7.9's fourth increment: entry 896 -> 951,
# total 8,212 -> 8,267. The last of the four gaps docs/M7_9_USER_SPACE_DRIVERS.md
# named: kernel-arm64-native now gates on COOKIE:M7.9:ATTACHED, ..:DISPATCHED,
# ..:SERVICED and ..:COMPLETED, in that order, proving the whole chain under
# QEMU the way M7.5i proved two-process preemption. Process A - reused after
# its M7.6a role concludes, not a third address space - attaches to a real
# capability minted at boot, arms the reused virtual-timer PPI as the stand-in
# device it just attached to, and is redirected by complete_after_switch into
# calling interrupt_complete the instant a delivery arrives, the same
# elr_el1-rewrite technique the send/receive redirects already use rather
# than a polling loop, so no code new to this proof needed anything beyond the
# instruction patterns M7.5f-M7.6a already proved under CI. Includes
# disarm_stand_in_device_source (+6 over the first version of this raise): a
# software comparator's condition does not self-clear the way a real device's
# would once serviced, so the first version of this proof left the reused PPI
# armed and it kept reasserting the instant interrupt_complete unmasked it -
# not a CI failure (the four markers still appeared, in order, before the
# 12-second QEMU budget ran out) but an interrupt storm burning the rest of
# that budget in a loop, not the single clean cycle the proof claims. Found by
# reading the actual CI log rather than trusting the green check - it repeated
# DISPATCHED/SERVICED/COMPLETED many times instead of once. Closes M7.9.
# Raised a sixteenth time, by M7.8.2's first increment: core 3,223 -> 3,327,
# total 8,267 -> 8,371. ipc_endpoint.hpp's own comment already named the gap:
# "the ThreadId-only path must fail closed for an M7.8 context-bound
# capability... Context-bound IPC gets its own ExecutionAuthority path in
# M7.8.2; until then a bound capability is intentionally unusable here." A
# capability minted via CapabilityTable::mint(ExecutionAuthority, ...) could
# be described and revoked but never actually used to send or receive -
# IpcEndpointTable::send/receive gain ExecutionAuthority overloads, sharing
# the rendezvous/pending-slot/reply-seal mechanics with the existing
# ThreadId overloads through two new private helpers (send_to_endpoint/
# receive_from_endpoint) rather than duplicating them, and Kernel::ipc_send/
# ipc_receive gain matching overloads so the new path is reachable from the
# same composition layer M7.9's interrupt_attach already lives at. Partial,
# and documented as such in the same diff: pending calls, reply seals and
# completed replies are still recorded and looked up by bare ThreadId, so a
# capability check happens at send/receive but nothing yet re-checks that
# the caller collecting a reply is still the generation the transaction was
# established under. That is M7.8.2's remaining gap, not this raise's.
# Raised a seventeenth time, by M7.8.2's second increment: core 3,327 -> 3,419,
# total 8,371 -> 8,463. Closes the gap the sixteenth raise's own justification
# named as remaining: IpcReplySeal, the private PendingSlot and the private
# CompletedSlot each gain an AddressSpaceIdentity field, populated only when
# the corresponding side used the ExecutionAuthority send/receive overload
# (additive - a default-constructed, invalid identity means "the legacy path
# was used here, nothing to compare" rather than "untrusted"). reply,
# reply_transaction and take_reply gain matching ExecutionAuthority overloads
# that check the recorded generation before delegating to the existing
# ThreadId implementation, refusing a same-thread-different-generation caller
# or server with the exact code a wrong thread already gets - not a
# distinguishing one, so a stale generation cannot learn "right thread, wrong
# incarnation" from the failure alone. Closes M7.8.2's second increment; the
# ExecutionAuthority-bound driver-capability question M7.9's own design doc
# left open remains separately undecided.
# Raised an eighteenth time, closing "the pre-MMU window" question
# docs/ROADMAP.md had left open since M7.5d: entry 951 -> 1,006, total
# 8,463 -> 8,518. FdtView::parse and discover_hardware ran with translation
# off, so every unaligned access the compiler emits inside them depended on
# -mstrict-align holding for every future contributor and file added to that
# target - a compiler flag, not an invariant the type system or a review
# enforces. cookie_aarch64_boot_main now builds and activates a second,
# minimal, throwaway identity map - covering exactly the kernel image (by
# segment, so .text/.rodata/.data+.bss keep the RX/R/RW split the real map
# gives them) and the DTB's exact extent - before FdtView::parse runs, then
# activates the real discovered map over it exactly as before. Not TF-A's or
# Linux's answer copied: both map a conservative fixed window generous enough
# to cover a DTB they have not yet measured, because at their equivalent point
# they have no cheaper option. Cookie already does: bounded_dtb's byte-wise
# read_be32 header parse - four single-byte reads and shifts, alignment-safe
# by construction - already determines the DTB's exact physical extent before
# any unsafe parsing runs, so the window mapped here is the DTB's real size,
# not a ceiling. The two new EarlyPageArena/EarlyStage1Builder/
# MachineAddressSpace instances share boot_physical_ledger with the real map
# rather than getting a dedicated one: every range either map creates uses the
# same permission, from the same add_identity_symbols calls, so the ledger's
# writable/executable-alias check - the only thing it rejects across spaces -
# never trips between them, and a dedicated ledger would have cost roughly
# 10 KiB of static state for isolation this throwaway map does not need.
# Corrected within the same change, before merge, by the CI run that proved
# both halves of that last sentence wrong: entry 1,006 -> 1,044, total
# 8,518 -> 8,556. The early map cannot share boot_physical_ledger, because
# the two maps really do disagree - each process's code page is writable in
# the early map (boot installs a program into it) and read-execute in the
# real one (the process runs it), which is a writable_executable_alias on
# the information a ledger has, and the fact that the maps are sequential
# rather than concurrent is not information a cross-space check can see. It
# gets its own ledger. And "minimal" turned out to be the hazard rather than
# the virtue: from the first activation until the real map replaces it the
# minimal map is the whole address space, so every region selected out of
# discovered RAM has to be added as it becomes known -
# extend_early_identity_map, called for the page-table region, the four
# process pages and the console. The console omission is why this was caught:
# the Data Abort vectored to a handler whose own uart_write took the same
# abort, and the machine looped in the vector reporting nothing at all.
# Raised a nineteenth time, by M7.11's first increment: machine 2,821 ->
# 2,920, entry 1,044 -> 1,099, total 8,556 -> 8,710. The kernel had one word
# for every synchronous exception that was not a system call - "EXCEPTION" -
# despite ExceptionFrame having carried esr_el1 and far_el1 since M7.5b and
# nothing ever reading them. describe_fault (machine) classifies the abort
# classes, the fault-status encodings and their translation-table level, and
# cookie_aarch64_unhandled_fault (entry) reports kind, cause, level,
# read/write, originating EL, and FAR/ELR/ESR. The decoder is pure and
# host-tested against hand-built syndromes, which matters more here than
# elsewhere: it is the code least exercised by a working system and most
# expensive to have wrong, since it only runs once something else already
# failed. Justified by its own cost during the pre-MMU window work in the
# same week - a level-3 translation fault on a write to an unmapped console
# presented as "COOKIE:PANIC:EXCEPTION", and recovering the address it had
# been holding all along took a QEMU instruction trace and several hours.
# M7.11 replaces the halt with delivery to a userland pager; this
# classification is what it will deliver, so the lines are not spent twice.
# Raised a twentieth time, by M7.11's second increment: machine 2,920 ->
# 2,997, entry 1,099 -> 1,105, total 8,710 -> 8,793. The physical ledger
# gains a reservation table - a physical range declared to hold kernel state,
# owned by one address space - and every map consults it: no EL0 translation
# of the range at all, no executable one, and no writable one from a space
# that does not own it. Boot declares the page-table arena before the first
# mapping is drawn from it.
#
# The 77 machine lines are worth stating as a ratio rather than a count,
# because this is the milestone the M7.0 claim is most likely to die in.
# M7_11_MEMORY.md's design projected that a VM subsystem could push core past
# 4,000; core is unchanged here, and the whole increment costs 0.9% of the
# trusted image. That is the shape the rest of M7.11 has to keep: this rule
# lands as a field and two loops on a structure the kernel already walks on
# every map, rather than as the separate capability-derivation tree the
# design deliberately declined to copy. The alternative was not cheaper - it
# was a second source of truth about physical memory that can disagree with
# the first, at more lines.
#
# The threat this closes is named in that design and was real in the code:
# "a process that can write its own page tables has no address space", and
# nothing expressed it. The pre-existing cross-space check answers only
# whether two mappings disagree about W^X, so two writable translations of
# the page-table arena - one the kernel's, one a process's - was a
# combination it had no reason to reject. The 6 entry lines are the boot
# routine saying which range that is; it is the only code that knows.
# Raised a twenty-first time, by M7.11's third increment: machine 2,997 ->
# 3,010, entry 1,105 -> 1,122, total 8,793 -> 8,823. Reservations gain a kind,
# and boot declares the kernel's writable image (__cookie_data_start..__bss_end)
# and its stack alongside the page-table arena.
#
# 13 machine lines for a second enforcement rule is the ratio this milestone
# needs to keep, and it is only that cheap because the increment before it put
# the table there. The 17 entry lines are two more boot declarations and the
# paragraph explaining why they are the weaker kind - the explanation is not
# free and should not be, because the reason is a constraint a reader cannot
# infer: Cookie translates through TTBR0 only, so EL1 executes under whichever
# process root is installed, and the kernel's globals must stay writable in
# every space or the kernel stops running the moment a process is scheduled.
# Owner-write-only becomes available for those ranges when M7.7 splits TTBR1,
# and the comment says so, so the day that lands nobody has to rediscover why
# the kind was chosen.
# Raised a twenty-second time, by the bounded-receive ABI: core 3,419 ->
# 3,427, machine 3,010 -> 3,015, total 8,823 -> 8,836. KernelCall::receive
# gains a relative nanosecond deadline in x2, and the AArch64 path refuses a
# non-zero one until the timer wiring exists.
#
# The 5 machine lines are that refusal, and they are the point of the change
# rather than an incidental cost. docs/M7_2_SERVER_LOOP.md establishes that no
# Cookie service main loop can be written on an unbounded-only receive, so the
# register contract had to be fixed early - a register contract is the
# expensive thing to change once callers exist. Accepting a deadline and
# ignoring it would have been free and much worse: a caller that asked for a
# bound and silently did not get one waits forever believing it will not.
# Raised a twenty-third time, by the receive-deadline mechanism: core 3,427 ->
# 3,479, total 8,836 -> 8,888. An armed receive can now carry an absolute
# deadline, the table reports the soonest one so a tickless scheduler arms a
# single timer for all waiters rather than one each, and an expired waiter can
# be taken out deterministically.
#
# 52 core lines is the largest single core raise in M7.11 so far and the first
# that is not zero, so it is worth saying what was not spent. There is no timer
# object, no wait set, no per-waiter callback and no priority queue - the table
# is the same fixed array it already was, scanned linearly, because
# max_threads is small and a heap would be mutable kernel structure bought for
# an asymptotic improvement nothing here needs. The ABI still refuses a
# non-zero deadline; this raise buys the mechanism, and the increment that
# removes the refusal buys the wake path.
# Raised a twenty-fourth time, by narrow_decision_timer: core 3,479 -> 3,491,
# total 8,888 -> 8,900. Twelve lines, and they exist because the obvious place
# to put this clamp was wrong in a way that would not have shown up until a
# receive deadline silently never fired.
#
# SchedulerDeadlineAuthority::accept_interrupt rejects a delivered deadline
# whose absolute time differs from current_, and rejects an interrupt arriving
# before current_ as `early`. Narrowing the armed timer after the authority has
# committed - clamping the ExecutionUniversePlan, which is where a reader
# naturally reaches - therefore arms hardware the authority does not know
# about, and the resulting interrupt is refused by its own staleness check.
# Clamping the Decision *before* prepare() keeps one armed time and one owner
# of it.
# Raised a twenty-fifth time, by WakeReason::deadline_expired and
# Rendezvous::expire_receive: core 3,491 -> 3,504, total 8,900 -> 8,913.
#
# The transition already existed - cancel_receive moves a receive-blocked
# thread to ready - so this buys a second entry point and a fifth wake reason
# rather than new machinery. It is a separate function rather than a reason
# parameter on cancel_receive because the two have different callers (endpoint
# retirement, and the kernel's own timer) and a shared parameter is a way for a
# later caller to pass the wrong one. Both retain the same guard: only a thread
# genuinely receive-blocked with no partner can be moved, so arbitrary thread
# wakeup stays impossible.
# Raised a twenty-sixth time, by threading the earliest bounded-receive
# deadline through PreemptionCoordinator: machine 3,015 -> 3,028, total
# 8,913 -> 8,926. core unchanged.
#
# Thirteen lines of parameter and narrowing call, defaulted to zero, so every existing caller keeps
# its exact behaviour - narrowing against no armed receive is the identity.
# It goes through the coordinator rather than around it because the clamp has
# to reach deadlines_.prepare(); anything that arms the hardware without the
# authority's knowledge produces an interrupt accept_interrupt discards.
# Raised a twenty-seventh time, completing bounded receive: core 3,504 ->
# 3,535, machine 3,028 -> 3,038, entry 1,122 -> 1,134, total 8,926 -> 8,979.
# KernelCall::receive now honours a deadline, and the ABI stops refusing one.
#
# The core lines are the drain (take the expired continuation, wake its thread)
# and the consuming expiry read. That read is the part worth defending: every
# other wake in this kernel is consumed by taking its payload - a reply is
# taken, a delivered message is taken - but an expiry has nothing to take,
# because the whole point is that no message arrived. Without a consuming read
# the completion path reports the same timeout on every switch back to the
# thread and overwrites its registers each time.
#
# The entry lines are the timer handler draining in a loop before it chooses,
# so a waiter whose deadline has passed is runnable in *this* decision rather
# than a slice later, and the two reschedule sites passing the earliest
# deadline - the reschedule after a blocking receive is what arms the timer at
# all, and on an idle system there is no later tick to fall back on.
# Raised a twenty-eighth time, by address-space release: machine 3,038 ->
# 3,114, total 8,979 -> 9,054. core unchanged.
#
# aarch64_release_address_space unmaps every mapping through the ordinary
# machine_unmap path, drops the space's reservations, retires the ASID and
# unbinds. Deliberately slower than walking the tables directly: bulk teardown
# that reimplements unmapping is a second place for break-before-make ordering
# to be got wrong, and M7.5e's own justification says a stale TLB entry after
# an unmap is a use-after-free with hardware caching it.
#
# It takes a RetiringAddressSpaceEpoch, and machine_release_address_space stays
# unsupported beside it. That is not an omission. Bulk release is safe only
# once no CPU can be executing in the space, and the portable signature takes
# the space and nothing else - it asks for a guarantee from an argument list
# that cannot express it. The typed entry point takes the token that does.
# Raised a twenty-ninth time, by donated page-table pages: machine 3,114 ->
# 3,168, total 9,054 -> 9,108. core unchanged.
#
# EarlyPageArena gains a second source - pages a process hands over one at a
# time - and works with no bump range at all, which is the post-boot shape.
# This is M7.11's "the kernel does not need a pool because the caller supplies
# the pages" made concrete, and it is what makes an address space creatable
# after boot: there is nothing to allocate from, so the caller allocates.
#
# The enforcement is a composition rather than a new check, and that is why it
# is cheap. aarch64_donate_table_page reserves the page as kernel_object before
# donating it, and reserve_physical already re-checks live mappings and refuses
# a range some process can still reach. A donor that has not unmapped its own
# page is therefore rejected before that page becomes a translation table it
# could keep writing - the hole the reservation work closed, reached from the
# other direction. The "declaring is not retroactive blessing" re-check written
# for boot, which boot itself never exercises, is exactly what does the work
# here.
# Raised a thirtieth time, by post-boot address-space creation: machine
# 3,168 -> 3,191, total 9,108 -> 9,131. core unchanged.
#
# Most of the 23 lines are two entry points and a comment explaining an
# ordering that looks arbitrary and is not. The API a reader expects - one
# create call that returns a usable space - cannot exist. A translation root is
# a page; after boot, pages are donated by a caller; donation reserves through
# a space that must therefore already be bound. Requiring a root at bind time,
# which is what boot's order implies and what the code used to enforce, made
# creating an address space depend on itself.
#
# So bind now accepts a rootless builder. That is safe rather than lax:
# install_leaf and leaf_pointer already return address_space_unbound when root_
# is zero, so every map, unmap and query fails closed in that window and only
# reserve_physical works - exactly what donation needs and nothing more. The
# relaxation is the smallest edit that breaks the cycle, and the alternative,
# letting the creator own the new space's root reservation, would have put
# teardown ownership in the wrong place.
#
# Raised again, by M7.11's address-space syscall surface: core 3,535 -> 3,605,
# total 9,131 -> 9,201. This is a raise to core, the category the QNX
# comparison is about, so it needs the better argument.
#
# What the 70 lines buy is the object-id encoding, not the two decoders. The
# decoders are almost trivial - reject zero, carry the rest through - and
# deliberately so: they do not re-check page alignment or page ownership,
# because aarch64_donate_table_page already refuses a page some process can
# still reach, and a second weaker copy of that rule beside the enforcing one
# is how the two come to disagree.
#
# The encoding is where the security content is. address_space_object_id folds
# the generation into the identifier rather than naming a slot, so a capability
# names one *lifetime* of an address space. M7.11's exit criteria require that
# a stale reference to a destroyed space fail closed "with the same error a
# wrong reference gets, not a distinguishing one" - and this shape delivers
# that with no check written for the purpose. A capability over generation 4 of
# slot 2 simply does not name generation 5 of slot 2, so the ordinary lookup
# misses and returns the ordinary not-found error. The alternative, a slot-only
# id plus a staleness comparison, costs fewer lines here and buys a check that
# can be forgotten at one call site plus an answer that tells a holder its
# space was replaced. Fewer lines, worse kernel.
#
# Two rights rather than one for the same reason interrupt sources have one:
# there are genuinely two authorities. A pager needs to hold spaces it services
# and must not be able to destroy them.
#
# Raised again, by M7.11's Kernel composition: core 3,605 -> 3,759, total
# 9,216 -> 9,370. The largest single raise to core this milestone, and the
# category the QNX comparison is about, so the argument has to be worth it.
#
# What it buys is the answer to "who may create an address space, who may
# destroy one, and what is a capability over a destroyed one worth" - the last
# being M7.11's stated exit criterion. The lines are mostly two capability
# helpers and a two-phase destroy, and three decisions inside them are the
# real content:
#
# Creation unwinds its own epoch. A capability table full at exactly the wrong
# moment would otherwise leave a space active that no capability names, and so
# one no destroy can reach, because destroy is reached through a capability.
# With 63 slots, a caller able to provoke that repeatedly exhausts address
# spaces without ever holding one.
#
# Destroy is two phases because ASID retirement genuinely is: begin invalidates
# the software epoch so nothing new binds, the machine layer then tears down
# translations and invalidates TLBs, and only then does complete release the
# ASID. Collapsing them hands an ASID to a new space while stale translations
# for the old one may still be cached.
#
# Completion re-checks that the capability names the space being retired.
# Without it, a holder of one space's destroy capability could complete
# another's retirement, releasing an ASID early - the same hazard from a
# different direction.
#
# AddressSpaceEpochAuthority also gains resolve(). A capability names a space
# by identity because that is what an object id can carry; retirement needs the
# epoch, which also carries an ASID. resolve() closes that gap while storing
# nothing, because the ASID was never independent information - acquire()
# derives it from the slot and active() already re-derives it to validate what
# it is handed.
#
# Raised again, wiring memory authority into address-space creation: core
# 3,759 -> 3,945, entry 1,380 -> 1,399, total 9,640 -> 9,845. This is the
# largest core raise of the milestone and it needs saying plainly: core is now
# 3,945, and docs/M7_11_MEMORY.md's own open question was whether a VM
# subsystem would push it past 4,000 - "the M7.0 claim the project rests on...
# this is the milestone most likely to break it." It has not, but it is 55
# lines away, and the next core increment should be treated as the one that
# decides that question rather than as another raise.
#
# What the lines buy is the referent the no-allocator decision was missing.
# address_space_create took a page number from a register and donated whatever
# it named; the reservation rules refused pages already reserved or user-mapped,
# but a process naming an arbitrary unclaimed page was appropriating memory
# nobody gave it. MemoryGrantAuthority records who holds which range, and the
# create syscall now takes a capability over memory instead of an address, so
# the claim is checked before anything is built from it.
#
# Taking a capability rather than an address also closes a disclosure: a
# syscall that accepts a physical address teaches every caller where in RAM its
# page tables live, and a process has no business knowing that.
#
# memory_grant.* moves out of the exclusion list in this change, which is the
# transition that list exists to make visible - it was excluded while nothing
# in the image included it, and counting it then would have overstated the TCB.
# It is in the image now, so it counts now.
#
# Raised again, wiring the fault-disclosure decision into the fault path: core
# 3,945 -> 4,120, entry 1,414 -> 1,442, total 9,860 -> 10,063.
#
# core crosses 4,000 lines here, and that deserves a note rather than silence
# because docs/M7_11_MEMORY.md spent this milestone asking about that number.
# It is no longer the binding figure: the target was restated on 2026-08-15
# (PR #120, docs/M7_0_KERNEL.md) to 4,529 *semicolons* - QNX's microkernel plus
# Proc - because Cookie's kernel does the job both of those do. The restatement
# landed before this increment and in its own diff, deliberately, so that
# crossing 4,000 lines here is a number moving past a superseded marker rather
# than a target being adjusted by the change that needed the room. The gap line
# this script prints is the one to read.
#
# What the lines buy: fault_region.* moves out of the exclusion list and into
# core, because cookie_aarch64_unhandled_fault now consults it - which is
# exactly the increment its exclusion note named as the one that would.
#
# The property is what a fault is allowed to say. A fault inside a declared
# region reports the REGION and never the faulting address, because the address
# at page resolution is the controlled-channel primitive
# docs/M7_11_FAULT_PRIVACY.md exists to withhold. The region's extent was chosen
# by the process that declared it, so the resolution of anything observable is
# the process's decision rather than the hardware's page size.
#
# The region path is consulted BEFORE the panic reporter, and that ordering is
# the property rather than a style choice: second, every fault would already
# have printed FAR on the way there. kernel-arm64-native checks the absence as
# well as the marker - a regression that printed the address would still emit
# COOKIE:M7.11:FAULT_REGION, so the marker alone proves nothing.
core_ceiling=4120
#
# Raised again, by M7.11's reclamation: machine 3,191 -> 3,229, entry 1,363 ->
# 1,380, total 9,585 -> 9,640. This makes the milestone's own threat model true
# rather than intended - "a freed page must not carry data to its next holder...
# a page whose contents the recipient must be trusted to ignore is not
# reclaimed, it is leaked."
#
# aarch64_release_address_space now zeroes every range a destroyed space owned
# before dropping its reservation, one range at a time. Two orderings in it are
# the safety argument rather than style. Zero before dropping, because an
# unreserved range is mappable and one zeroed after release could be claimed
# and read in between. And one at a time, because a failure part-way then
# leaves the rest reserved rather than unreserved and still carrying contents.
#
# The zeroing writes through a volatile pointer. This is memory nothing reads
# again through any path a compiler can see - the textbook shape of a dead
# store - and a compiler that removed it would delete the security property
# silently while leaving any test that checks for zeroes still passing,
# because the range would often be zero anyway.
#
# The entry lines are the proof, and it is deliberately a pair: the boot proof
# asserts the table pages are NON-zero before the destroy and zero after. The
# first half is what stops the second from being vacuous - without it the check
# would pass on a range that was always zero, and keep passing if reclamation
# were deleted. It reads through volatile too, since the writes it is looking
# for happen in another translation unit and the two reads would otherwise fold
# into one.
#
# Raised again, making an EL0 fault kill the thread instead of the machine:
# machine 3,229 -> 3,232, entry 1,442 -> 1,463, total 10,063 -> 10,087. core
# untouched.
#
# docs/M7_11_MEMORY.md's fault path said halting "is correct only while there
# is nothing that could possibly respond". A fault at EL0 is one process's
# problem: the thread that took it may have to die, the machine has no reason
# to. cookie_aarch64_el0_fault reports the region, destroys the faulting
# thread, chooses what runs next and returns - the same shape the timer path
# already uses, because it is the same question.
#
# The three machine lines are the split that makes it possible: the lower-EL
# sync vector now calls a handler that is not [[noreturn]]. Current-EL faults
# keep the [[noreturn]] path, and that asymmetry is deliberate - there the
# kernel itself faulted and there is no smaller thing to kill.
#
# "Nothing runnable" halts with a name rather than trapping, because it is a
# real outcome and not an invariant violation: it is what happens when the last
# thread faults.
machine_ceiling=3232
discovery_ceiling=1272
#
# Raised again, fixing the UNIVERSE_MARKER race in the boot proof: entry
# 1,133 -> 1,148, total 9,201 -> 9,216. kernel-arm64-native failed once with
# COOKIE:PANIC:UNIVERSE_MARKER and passed on a bare re-run, which is the worst
# shape a gate can have here - this job is the only thing that actually boots
# the kernel, so a failure cured by re-running teaches everyone to re-run, and
# makes a real boot regression indistinguishable from noise.
#
# The race was real and in the proof, not the kernel. B is first entered by
# frame restore from inside the timer handler, and that handler arms the next
# deadline before returning, so the timer can assert again between the ERET
# into B and the retirement of B's first instruction - the movz that sets its
# universe marker. In that window B is running with x19 still holding the zero
# from its admitted frame, and the check fails on a kernel behaving correctly.
# Closed by establishing B's marker in the admitted frame, so it holds from
# instruction zero.
#
# A deliberately keeps its old arrangement: its marker is load-bearing in two
# syscall-path gates that exist to prove A's own movz executed and survived
# two round trips, and seeding it would make both vacuous. A needs no seed
# because nothing arms a deadline before its first instruction, which the EL0
# start already asserts.
#
# Most of the 15 lines are the diagnostic. The first occurrence printed the
# panic name and nothing else, so working out which of the three disjuncts
# could even fire took a reading of the entire proof. It now reports which one
# failed and with what values, on the same argument the M7.11 fault decoder
# was justified on: the evidence exists at the moment of failure and costs
# nothing to print, and reconstructing it afterwards costs hours.
#
# Raised again, by M7.11's boot proof: entry 1,148 -> 1,185, total 9,370 ->
# 9,407. Everything M7.11 had landed was host-tested, and this milestone's
# exit criteria are written in terms of a running system - a proof that exists
# only on the host cannot meet them. kernel-arm64-native now gates on
# COOKIE:M7.11:SPACE_CREATED, ..:SPACE_DESTROYED and ..:STALE_REFUSED: an
# address space created after boot, authorized by a capability, retired in the
# two phases the ASID lifecycle requires, and a capability over the identity it
# used to have refused afterwards.
#
# It deliberately does not donate a page or map anything, so it proves the
# kernel composition rather than the whole of creation-after-boot. The reason
# is recorded at the call site and is not a schedule compromise: a donated
# table page must be reserved kernel_object owned by the new space, while
# EarlyStage1Builder writes tables through their raw physical address, which
# under live translation needs the page mapped writable in the active space -
# which is not the new one. forbidden_by_reservation refuses exactly that, and
# correctly, since that rule is what stops a donor keeping write access to what
# has become a translation table. Relaxing it to make a proof pass would be
# weakening a security rule to fit a demo. The ordering that satisfies both is
# a real design decision and is left open rather than pre-empted here.
#
# Raised again, completing M7.11's boot proof: entry 1,185 -> 1,214, total
# 9,407 -> 9,436. The proof now donates pages, builds a translation root from
# one of them, and maps a page into the created space -
# COOKIE:M7.11:SPACE_MAPPED between SPACE_CREATED and SPACE_DESTROYED.
#
# The previous raise recorded this as blocked by forbidden_by_reservation and
# left it open. That analysis was wrong in its premise and the correction is
# worth keeping: early_identity_space has its own ledger, not
# boot_physical_ledger, so the reservation a donation takes cannot see the
# early map's writable mapping of the same page, and nothing refuses it. The
# separation is deliberate - see early_identity_ledger's declaration, where it
# exists because a_code is writable in the early map and read-execute in the
# real one, which a shared ledger would call a writable_executable_alias.
#
# Relying on it here is sound for the same reason it exists: the two maps are
# sequential rather than concurrent, the early one is kernel-only, and it is
# abandoned before any process runs, so no EL0 translation of a table page
# ever exists. The mapped page is deliberately not one of the donated ones,
# because a user-accessible translation of a live translation table is exactly
# what the kernel_object reservation is there to refuse.
#
# Raised again, by M7.11's EL0 dispatch: entry 1,214 -> 1,363, total 9,436 ->
# 9,585. The largest entry raise of this milestone, and it buys the thing the
# milestone existed for: a *process* can create an address space and destroy
# it. kernel-arm64-native gates on COOKIE:M7.11:EL0_CREATED and
# ..:EL0_DESTROYED, after the M7.9 device proof concludes.
#
# Most of the lines are the pool and the two halves of create. The pool is two
# slots, not one, so it is a bound with a real exhaustion answer rather than a
# single slot pretending to be a pool - and exhaustion returns to the caller
# instead of halting, which is the no-allocator decision showing up exactly
# where it should: running out is something a caller is told and can act on.
#
# Two things here are load-bearing and easy to undo by accident. The syscall
# entry now admits CallAuthority::process_control, and immediately refuses
# every process_control call other than the two implemented - widening an
# authority check is not the same as implementing what it lets in, and the
# other calls carrying it would otherwise fall through to the yield tail and
# get a wrong answer rather than a refusal.
#
# And the root page EL0 names is in the kernel manifest, so it is mapped in
# all three spaces. That is not tidiness: the kernel builds the created space's
# tables from inside A's syscall, and Cookie translates through TTBR0 only, so
# EL1 is running under A's root at that moment. It is also the concrete reason
# donation reserves kernel_private - three kernel-side writers of a page about
# to become a translation table is what kernel_object forbids and what
# TTBR0-only translation forces.
#
# Raised again, proving donation is a loan: entry 1,399 -> 1,414, total 9,845 ->
# 9,860. core is untouched.
#
# The EL0 proof now creates a space, destroys it, and creates a second one with
# the *same* memory capability, gated on COOKIE:M7.11:EL0_GRANT_REUSED. That
# second create already worked before this change, and that was the problem: it
# worked because the reservation had been dropped and the grant was never
# invalidated, which is an accident rather than a decision.
#
# The decision, recorded here because the code alone cannot show a road not
# taken: donating a page does NOT consume the grant. The alternative - revoke
# on donate, re-issue on destroy - would make the kernel an origin of grants,
# and boot being the only origin is what stops the grant table becoming a pool
# the kernel dispenses from. Under the loan reading the holder's authority
# never left, so there is nothing to hand back; what ends at destroy is the
# space's use of the page, not the holder's claim on it.
entry_ceiling=1463
total_ceiling=10087

# The aspiration from docs/M7_0_KERNEL.md, for the gap report. This is not a
# ceiling and is not enforced. It is printed on every run so that the distance
# is a thing the project keeps seeing rather than a thing it stops mentioning.
qnx_microkernel=605
qnx_proc=3924
qnx_whole_os=15930

# ---------------------------------------------------------------------------

count_lines() {
    # count_lines <file...> - files are known to exist, see the check below
    local total=0 file lines
    for file in "$@"; do
        [ -f "$file" ] || continue
        lines="$(awk "$strip_comments" "$file" | cut -d' ' -f1)"
        total=$((total + lines))
    done
    printf '%s' "$total"
}

# The same stripped text, counted the way the QNX paper counted its own: by
# semicolons. Not the enforced number - nothing is capped on this - but the only
# one that can honestly be divided by 605. See the gap section at the foot.
count_semicolons() {
    local total=0 file semis
    for file in "$@"; do
        [ -f "$file" ] || continue
        semis="$(awk "$strip_comments" "$file" | cut -d' ' -f2)"
        total=$((total + semis))
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

core_semicolons="$(count_semicolons "${core_files[@]}")"
total_semicolons="$(count_semicolons "${counted[@]}")"

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
#
# This section was wrong in two independent directions until the QNX paper was
# read rather than cited. Both corrections are recorded in
# docs/REFERENCE_NOTES_2026_08_14_QNX.md; the short forms:
#
#  1. Metric. QNX's figures are semicolon counts, stated outright in the paper:
#     "all of the source line counts in this paper were generated by counting
#     the semicolons in the C source files." Dividing Cookie's non-blank
#     non-comment line count by 605 compares two different quantities and
#     roughly doubles the apparent gap. core_semicolons is the like-for-like
#     number and is reported beside it.
#
#  2. Scope. The QNX microkernel implements four services - IPC, low-level
#     network communication, process scheduling, interrupt dispatching - and
#     memory management is not among them. It lives in Proc, a user-space
#     resource manager of 3,924 semicolons. M7.11 is putting memory management
#     inside Cookie's kernel, so 605 stops being the comparable figure for it;
#     605 + Proc is. The reverse also holds and is printed because it cuts the
#     other way: QNX's microkernel carries low-level networking, which Cookie's
#     core does not.
#
# NEITHER CORRECTION IS HEADROOM. The ceilings above are deliberately untouched
# by both, and stay counted in lines: they ratchet Cookie against its own past
# in one metric, and that job never depended on what QNX counted. What changes
# here is only what this script is entitled to claim.
# The figure core is actually held against, decided 2026-08-15 and recorded in
# docs/M7_0_KERNEL.md: QNX's microkernel plus Proc, because Cookie's kernel does
# the job both of those do. Memory management is not in QNX's 605 - it is in
# Proc - and M7.11 put it inside Cookie's kernel deliberately.
#
# Printed as a distance, not enforced as a ceiling. The ratchet above is what
# stops growth; this says how far the thing being defended actually is, so the
# day core passes it is a day the build says so rather than one nobody notices.
qnx_comparable=$((qnx_microkernel + qnx_proc))
ratio_tenths=$(((core_semicolons * 10 + qnx_comparable / 2) / qnx_comparable))
if [ "$core_semicolons" -le "$qnx_comparable" ]; then
    note "$(printf 'gap   core is %d.%dx its comparable target - %d semicolons against %d (QNX microkernel + Proc), %d to spare' \
        $((ratio_tenths / 10)) $((ratio_tenths % 10)) "$core_semicolons" "$qnx_comparable" \
        $((qnx_comparable - core_semicolons)))"
else
    note "$(printf 'gap   core has PASSED its comparable target - %d semicolons against %d (QNX microkernel + Proc), over by %d' \
        "$core_semicolons" "$qnx_comparable" $((core_semicolons - qnx_comparable)))"
fi
note "$(printf 'gap   against the microkernel alone it is %d against %d - but that half does no memory management and Cookie'"'"'s does' \
    "$core_semicolons" "$qnx_microkernel")"
note "$(printf 'gap   core in this gate'"'"'s own metric is %d lines; the whole trusted image is %d lines / %d semicolons' \
    "$core_lines" "$total_lines" "$total_semicolons")"
note "$(printf 'gap   QNX for scale, all semicolons: microkernel %d (no memory management), +Proc %d, whole OS %d' \
    "$qnx_microkernel" "$qnx_proc" "$qnx_whole_os")"

escaped="${report//\%/%25}"
escaped="${escaped//$'\n'/%0A}"

if [ "$failed" -ne 0 ]; then
    echo "::error title=Kernel line count (${label})::${escaped}"
    exit 1
fi

echo "::notice title=Kernel line count (${label})::${escaped}"
