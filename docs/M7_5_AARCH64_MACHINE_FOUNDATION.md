# M7.5 — AArch64 machine foundation

## Implementation authority

**References teach principles. Cookie determines implementation. External systems are not the design specification.**

M7.5 is the point where the host-testable Cookie Kernel begins acquiring real
machine mechanisms. The goal is not an AArch64-shaped host simulator. The goal
is a machine backend that can eventually boot Cookie on the reference emulator
with no Linux kernel underneath it, while keeping every architecture-specific
assumption behind `machine.hpp`.

## Reference machine

The first runnable target is QEMU's AArch64 `virt` machine. It is an emulated
reference platform, not Cookie's hardware ABI and not a promise that production
phones resemble it.

QEMU documents two constraints that become Cookie rules:

1. the `virt` machine is deliberately generic rather than a model of a physical
   board;
2. except for the documented flash/RAM anchors, device addresses and interrupt
   routing may change and are described by the generated device tree.

Cookie therefore does **not** hard-code PL011, GIC, virtio, PCI or other device
addresses into portable kernel code. The future emulator boot path consumes the
DTB and turns it into a bounded platform description. A device driver receives
only the MMIO/interrupt authority selected from that trusted description.

For the standalone Cookie image we prefer QEMU's ELF/bare-metal path rather than
pretending the Linux direct-kernel boot protocol is Cookie's boot ABI. The exact
emulator invocation and a versioned `virt` machine type will be frozen when the
first boot gate lands so CI does not silently inherit a changed virtual board.

## AArch64 contract

The current machine contract uses a 4096-byte page. Arm's VMSAv8-64 translation
architecture explicitly supports a 4 KiB granule with 4 KiB level-3 pages. M7.5
keeps that page size but does not freeze a physical-address width or assume a
particular number of translation levels before reading the processor's
capabilities.

Architecture code remains narrowly scoped:

- system-register access;
- exception entry/return;
- saved register contexts;
- translation-table programming and TLB maintenance;
- generic-timer programming;
- interrupt-controller adaptation;
- the minimum early-boot transition needed to enter the portable kernel.

Scheduling, capability policy, message semantics, driver policy and service
policy remain above `machine.hpp`. Assembly may implement a mechanism; it may
not decide policy.

## M7.5a — first real machine operations

This slice implements the first two machine operations that need actual AArch64
rather than a host model:

- `machine_page_size()` reports the reviewed 4 KiB architectural contract;
- `machine_set_timer()` and `machine_monotonic_nanoseconds()` use the AArch64
  Generic Timer counter/frequency system registers.

Timer conversion is integer-only, bounded, and independently host-testable.
`CNTFRQ_EL0` is treated according to its architectural 32-bit frequency field,
which keeps multiplication bounds explicit. A zero frequency is refused.
Relative nanoseconds are converted to an absolute physical-counter deadline;
representational overflow is refused rather than wrapped into an earlier timer.
Monotonic nanoseconds saturate on representational overflow instead of wrapping
backwards.

The backend is compiled as its own archive on the native AArch64 CI runner. It
is deliberately **not** linked into the hosted test runtime yet. Operations that
have no real mechanism still return `machine_errors::unsupported`, while the
void context-switch operation traps unconditionally if reached. A no-op context
switch would be more dangerous than an incomplete port because it would let a
boot attempt appear to progress on behavior no processor implements.

## Why this remains small

Current QNX documentation reinforces a principle already present in the supplied
microkernel material: its microkernel is primarily high-level code, not an
assembly project; size and performance come from refined objects and algorithms,
with file/device services outside the microkernel. Cookie follows the same
principle without copying QNX calls or POSIX personality. The portable C++ state
machines remain the kernel; assembly is a thin machine adapter.

Android and Apple secure-boot documentation reinforce a separate boundary: each
boot stage verifies the next before use and the trust chain begins from a
hardware-protected/immutable root. Those are M5/M8 boot-provider obligations,
not a reason to put signature verification into the scheduler or MMU layer.
Likewise BitLocker's TPM/boot binding reinforces tying key release to measured
boot state, not importing Windows TPM/UEFI policy into Cookie.

The cryptographic research set (AES/SHA, ring signatures, ZK systems and the
supplied PQ/signature papers) is intentionally not pulled into this machine
slice. A page-table or timer implementation gains no security from carrying
extra cryptographic code. Crypto enters only where an explicit Cookie threat
model needs it.

## No-Linux migration rule

"No Linux" means Linux mechanisms are replaced, not merely deleted. The existing
M7.2 ceiling therefore stays active until its count reaches zero. Hosted Linux
builds remain a development/test convenience while migration is in progress;
they are not the product substrate.

The replacement order remains:

1. finish enough AArch64 machine support to boot and schedule on the emulator;
2. replace `SOCK_SEQPACKET`/`SCM_RIGHTS`/`SCM_CREDENTIALS` with Cookie rendezvous,
   capability transfer and kernel-authenticated sender identity;
3. replace fork/exec/pidfd/signal supervision with Cookie address spaces,
   threads and peer-death semantics;
4. replace memfd display buffers with Cookie mappings/capabilities;
5. remove seccomp/Landlock/no_new_privs substrate code because Cookie grants no
   ambient authority that those mechanisms need to claw back.

A Linux-dependent file leaves the permitted list only after its Cookie
replacement is gated. Deleting it first would make the migration look complete
while removing functionality or confinement.

## Next machine slices

### M7.5b — exception and context entry

- bounded saved AArch64 register frame;
- exception vector table behind the machine directory only;
- system-call entry validates the frozen kernel ABI before dispatch;
- real `machine_prepare_context()` and `machine_switch_context()`;
- normalize supported emulator handoff levels into the reviewed kernel EL rather
  than assuming a particular incoming EL.

### M7.5c — translation tables

- read architectural MMU capabilities;
- stage-1 4 KiB translation tables;
- W^X and guard-page rules enforced by the same contract already tested on host;
- machine-wide physical ledger drives hardware admission;
- break-before-make/TLB maintenance rules stated and tested where applicable;
- no heap in mapping metadata.

### M7.5d — interrupt and timer delivery

- DTB-discovered GIC implementation for the reference machine;
- per-source mask/unmask/acknowledge;
- timer interrupt feeds the tickless scheduler deadline;
- no user driver code executes in interrupt context.

### M7.5e — first standalone emulator boot

- ELF Cookie Kernel image;
- bounded DTB parser/platform description;
- early stack and physical-memory bootstrap;
- serial-only diagnostic output as a bring-up aid, not a public ABI;
- boot → timer → scheduler → first kernel thread gate under `qemu-system-aarch64`;
- no Linux kernel or Linux userspace in the guest.

Only after that gate is green does the IPC migration begin. That order makes the
first Linux dependency removal rest on a machine capable of replacing it rather
than on documentation saying it eventually will.
