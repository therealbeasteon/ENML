# The kernel question

**Does ENML have a kernel? No, and it is not written to acquire one.** Linux is
the substrate. `PROJECT_VISION.md` froze that: Linux is the private kernel and
hardware substrate, not ENML's public operating-system personality; ENML
services and typed interfaces are the stable platform above it, and the rule is
to prefer upstream Linux interfaces with small reviewable board-support changes
rather than forking kernel behaviour for convenience.

That is a decision, not an omission. This document exists because it is a
decision with a cost, and the cost should be written down rather than discovered
later.

## What the references say a microkernel buys

The QNX architecture paper is the most useful data point, because it reports
measured numbers rather than claims:

| Component | Lines | Size |
| --- | --- | --- |
| Microkernel | 605 | 7 KB |
| Process manager | 3,924 | 52 KB |
| Filesystem | 4,457 | 57 KB |
| Device manager | 2,204 | 23 KB |
| Network + drivers | 2,259 | 35 KB |
| **Whole OS** | **15,930** | **204 KB** |

The kernel implements four things - IPC, low-level network transport, process
scheduling, interrupt dispatch - behind fourteen calls, and fits inside an 8 KB
on-chip cache. Everything else, including the filesystem and every driver, is an
ordinary memory-protected process that can be started and stopped at runtime.

The performance objection to this shape turns out to be wrong in the measured
case: message passing was ~14x faster than the monolithic UNIX it was compared
against, sequential file reads 5-8x, pipe I/O ~2x. Two design choices did that
work - multipart messages, so a message assembled from a header and scattered
buffers needs no copy to make it contiguous, and interrupt handlers that live
inside the user-space driver process, connected to a vector by a system call.

The maintainability argument is the one that matters most for ENML's charter:
in a monolithic kernel all kernel code shares one address space, so the risk
that a driver corrupts an unrelated subsystem is real and must be re-evaluated
every time a driver is linked in. With resource managers in separate address
spaces, a fault is confined to the subsystem that caused it.

The paper also warns against the obvious mistake, and it is worth quoting the
shape of it: adding calls to the microkernel to make individual operations
faster eventually grows it back into a monolithic kernel, with all the
limitations that implies.

## Where ENML actually stands

ENML's *service architecture* is already the shape those references describe.
Services run as separate processes with typed identities; they talk over
bounded, explicitly encoded messages rather than shared structures; the
supervisor restarts them individually; capabilities are brokered and revoked
rather than ambient; a service dying does not take the system with it, and the
M2.10 fixture exists precisely to prove a client survives one restarting
underneath it.

What ENML does **not** get from that is a small trusted computing base. The
boundaries constrain applications and services. They do not constrain the
kernel, and the kernel is Linux - on the order of tens of millions of lines,
against QNX's fifteen thousand for an entire operating system.

So the honest statement of where the project is:

> **ENML is microkernel-shaped in its service architecture and monolithic in its
> trusted computing base.**

Both halves are true, and the second is a material gap between the charter's
ambition - "as secured and hardened as possible" - and what the current design
can deliver. No amount of work above the kernel closes it. It is recorded here
so that nobody reads the service architecture and concludes the TCB is small.

## What ENML actually requires of a substrate

Whatever sits underneath, these are the things ENML's existing code depends on.
This list is the useful form of the question "do we have what is needed", since
it can be checked against any candidate rather than argued about:

**Depended on today, and provided by Linux:**

- Address-space isolation between processes.
- A datagram IPC primitive with message boundaries preserved, carrying both file
  descriptors and kernel-attested peer credentials. ENML uses `SOCK_SEQPACKET`
  with `SCM_RIGHTS` and `SCM_CREDENTIALS`; the credential attestation is what
  makes every typed identity in the tree meaningful, and it must come from the
  kernel rather than from the peer.
- Syscall filtering and filesystem-scoped confinement for services - seccomp and
  Landlock.
- Per-process resource limits, which the M4.2 budget gate measures against.

**Required by the design, and not yet obtained from anywhere:**

- A verified boot chain terminating in a measured root filesystem. M5.0 designs
  the evidence; nothing produces it.
- IOMMU programming, so `DmaCapability::iommu_confined` is enforced rather than
  recorded (M6.0).
- Micro-architectural partitioning, so `PartitionLedger` accounts for something
  real (M6.2). Linux exposes almost none of this; it is largely a platform
  question.
- A TEE client path, for which the Linux generic TEE interface is the neutral
  option identified in the portability work.

**Nothing has run on real hardware.** Every property above is established on
host or emulated builds.

## If the decision were revisited

It has not been, and this document does not propose revisiting it. But the
conditions under which it *should* be are worth naming now, while they are easy
to state:

- If ENML's threat model comes to include a hostile local attacker with kernel
  exploit capability, the Linux TCB is the whole answer to what they get, and no
  ENML boundary changes it.
- If a certification target requires an evaluable TCB, tens of millions of lines
  is not one.
- If the "lightweight" goal comes to mean a device class where a 204 KB
  operating system is the right size rather than a 30 MB one.

Short of those, the pragmatic case for Linux is strong and unchanged: drivers,
filesystems, networking, power management and an entire hardware ecosystem exist
for it and would otherwise all have to be written. The references support that
reading too - the portable-TEE work runs *beside* a full OS rather than
replacing it, and reaches a dozen platforms by keeping its own trusted component
small.

The defensible middle, and the one ENML is already on, is to keep shrinking what
has to be trusted above the kernel while being honest that the kernel itself is
not small. The device access model (M6.0) is exactly that move: it refuses to
call a driver confined when the platform cannot confine it.
