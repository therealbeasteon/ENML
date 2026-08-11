# M7.0 - Cookie Kernel

The operating system is **Cookie**; its microkernel is the **Cookie Kernel**;
the security architecture both carry is **EMNL**. See `docs/NAMING.md` for why
those are three names and not one.

## The decision

Cookie will have its own microkernel. Linux stops being the substrate and
becomes, at most, a development host.

This reverses the position frozen in `PROJECT_VISION.md` and analysed in
`docs/SUBSTRATE.md`, which set out the conditions under which the decision
should be revisited. The project owner has revisited it. The reason given is
independence: a Linux vulnerability should not be an ENML vulnerability.

The fuller reason, as the owner put it, is that the five properties this
project exists for - security, stability, hardware neutrality, trust and
lightweightness - cannot be *guaranteed* while sitting on someone else's kernel.
That is the stronger form of the argument and it is correct. Each of the five is
inherited rather than chosen when the kernel belongs to someone else:

- **Security** is bounded by their trusted computing base, not ENML's.
- **Stability** is bounded by their release cadence and their regression policy.
- **Hardware neutrality** is bounded by which platforms they chose to support,
  and by how much of their driver model leaks upward.
- **Trust** cannot be reasoned about at all across a boundary nobody here can
  read completely.
- **Lightweightness** is bounded by their definition of small, which for a
  general-purpose kernel is necessarily not this project's.

A property you do not control is not a guarantee, it is a dependency that has
so far behaved. That is the case for owning the kernel, and it is stronger than
the CVE-independence argument on its own.

That reason is sound, with one correction that shapes everything below.
Independence from Linux's defect history is real and immediate. Independence
from *defects* is not - a new kernel begins with its own, and nobody has audited
them. The security case therefore does not rest on being different. It rests on
being **small enough to actually review**, and it is only redeemed if the kernel
stays small. Every argument in this document is downstream of that.

The references make the target concrete rather than aspirational. QNX shipped an
entire operating system - filesystem, device manager, networking, drivers - in
15,930 lines and 204 KB, on a kernel of 605 lines and 7 KB implementing four
services behind fourteen calls. That is the class of artefact the Cookie Kernel is aiming at.
Against tens of millions of lines of Linux, a 605-line kernel is not a smaller
version of the same thing; it is a different kind of object, one a person can
read completely.

## What goes in the kernel

Four responsibilities, following QNX directly, because the decomposition has
been demonstrated rather than argued:

1. **Address spaces and threads** - creation, destruction, scheduling.
2. **Synchronous message passing** - the send/receive/reply triple, with
   messages copied directly between address spaces and never queued in the
   kernel.
3. **Interrupt dispatch** - delivering an interrupt to a handler, which lives in
   a user-space driver process.
4. **Capability transfer** - moving an authority from one address space to
   another as part of a message.

The fourth is ENML's addition and the only one QNX does not have as such. It is
not scope creep: ENML's entire security model is that authority is an object
that is granted, brokered and revoked rather than an ambient property of a
process. If capability transfer is not a kernel primitive, it becomes a
convention implemented above the kernel, and a convention is exactly what an
attacker with a memory-safety bug ignores.

## What does not go in the kernel

Everything else, and this list is the one to defend when it is inconvenient:

- **No filesystem.** A storage service owns durable state, as it already does.
- **No drivers.** Drivers are user-space processes under M6.0 device access
  policy, with interrupt handlers connected to vectors by a kernel call. This is
  the microdriver split with the security correction ENML already made - bounded
  MMIO windows, no port I/O authority, no isolation claim a platform cannot
  back.
- **No network stack.** QNX put low-level network transport in the kernel for
  distribution transparency. ENML is a phone, not a compute cluster, and that
  transparency is not worth kernel lines.
- **No memory allocator for user processes.** The kernel manages address spaces
  and page mappings; allocation policy sits above it.
- **No POSIX.** Compatibility, if it is ever wanted, is a library and a service,
  never a kernel obligation.
- **No paging to storage.** A phone with no swap has one less kernel subsystem
  and one less side channel.

The rule to apply when something wants in: adding a call to make one operation
faster is how a microkernel grows back into a monolithic kernel, and the calls
that matter get slower as it does. If an operation is not on the critical path,
it belongs in a service.

## What ENML already has that survives

This is not a restart. The layers above the kernel were built against a typed,
bounded, brokered model that does not assume Linux, and most of it transfers:

- The wire-format discipline - explicit little-endian, fixed capacity, unknown
  discriminants rejected, reserved fields must be zero - applies unchanged, and
  now applies to the kernel ABI itself.
- Typed identities, the service broker, capability handoff and revocation are
  the same design; what changes is that the kernel enforces them rather than a
  Unix socket carrying `SCM_CREDENTIALS`.
- M6.0 device access policy was written to describe a driver's authority
  independently of who enforces it. It now describes what the kernel grants.
- M6.2's partition ledger was written as platform-independent accounting for a
  mechanism the OS must own. ENML now owns the mechanism too.
- M5.x boot state, M4.x UI and consent, the storage and key services, and every
  fuzz target and gate are unaffected.

What does **not** transfer is the substrate work in M6.3, which probes a Linux
kernel for facilities ENML will now implement itself, and the assumption
throughout the service layer that `SOCK_SEQPACKET` is the transport.

## Order of work

1. **The ABI** - the complete system call surface, defined as a bounded typed
   format before any implementation exists, so that the ceiling on kernel size
   is a design artefact rather than an outcome. This is where M7.0 starts.
2. **A host-testable kernel core** - the message-passing and capability logic as
   pure state machines, tested and fuzzed on the development host without any
   hardware. ENML's existing gates run against it immediately.
3. **The machine layer** - context switch, MMU, timer, interrupt controller.
   Small, architecture-specific, and the only part that cannot be tested on the
   host.
4. **Boot on an emulator**, which is what M5.0 already identified as the
   reference platform, before any physical board.

## The honest position

This is a multi-year direction, not a milestone. The intermediate state - an
ENML kernel that is less complete than Linux and less audited than it will
eventually be - is genuinely worse than what it replaces, for as long as it
lasts. That is the price of the decision and it should be visible in the ledger
rather than discovered.

The measure of whether it was worth it is a single number: how many lines of
code have to be trusted. If ENML's kernel reaches Linux's size, the exercise has
failed regardless of what else it achieved.
