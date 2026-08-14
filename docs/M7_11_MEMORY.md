# M7.11 - Memory as a first-class object

**Status: design.** No code yet. `docs/ROADMAP.md` added this milestone
(Phase 2b) after a gap review found virtual memory absent from the phase list
rather than deferred within it. This document makes the decisions that have to
be made before any of it is written, because the central one - whether the
kernel allocates - cannot be revisited once subsystems depend on the answer.

Cookie's kernel today can create address spaces exactly once, at boot, from a
plan computed before any process exists. Everything M7 built on top of that -
epochs, capabilities, IPC, interrupts, preemption - is real and tested. The
memory underneath it is not: it is a boot artifact that no running code can
change. This milestone is where that stops being true, and it is the largest
single addition the kernel will take.

## What already exists

More than the roadmap line suggests, and the shape of it constrains the design.

**A physical ledger the kernel already consults on every mapping.**
`aarch64_mapping_state.hpp`'s `NativePhysicalLedger` records, for each mapped
physical range, which address space mapped it and with what permissions, and
`map_impl` walks it on every map to refuse a `writable_executable_alias` -
a range writable in one space and executable in another. This is the fact this
milestone builds on and it is easy to miss: **Cookie already has a kernel-side
authority table over physical memory.** It is consulted on every mapping, it is
already trusted, and it already answers a question about physical ranges that
crosses address-space boundaries.

**Generation-bound epochs.** `AddressSpaceEpochAuthority` (acquire /
`begin_retire` / `complete_retire`) and `ProcessTranslationTable` (bind /
resolve / retire) already implement the hard part of safe teardown: a
three-state lifecycle where a reference minted against a previous incarnation
of an address space fails closed rather than resolving to its successor. M7.8
extended the same generation discipline through capabilities and IPC reply
seals. Address-space *destruction* does not exist, but the machinery that makes
destruction safe to observe does.

**Per-mapping teardown.** `machine_unmap` performs real TLBI-backed removal of
an individual mapping. This is not a stub.

**A page-granularity table builder.** `EarlyStage1Builder` maps, unmaps, and
tracks a `{uninitialized, building, sealed, retiring}` lifecycle, with
`TranslationRootSealer` gating activation on a sealed root.

## What is missing

Stated as concretely as possible, because each of these is a specific line of
code that says so.

**There is no allocator.** `EarlyPageArena` is a bump pointer over a fixed
physical range: `allocate_page()` moves `next_` forward and nothing ever moves
it back. There is no free. Its header says the rest plainly - intermediate
table pages "are not reclaimed until the general physical allocator owns
page-table lifetime." That allocator is this milestone.

**There is no address-space destruction.** `machine_release_address_space`
returns `machine_errors::unsupported`, with a comment recording why: "Bulk
release waits until the scheduler/process lifetime layer can prove no CPU is
executing in this space." That layer now exists - `PreemptionCoordinator` knows
what is running and the epoch authority can retire a generation. The blocker
named in that comment has been removed and the function has not been revisited.

**There is no fault path.** `cookie_aarch64_unhandled_exception` prints
`COOKIE:PANIC:EXCEPTION` and halts. It does not read `ESR_EL1` or `FAR_EL1`. A
translation fault is not a question the kernel can answer; it is the end of the
machine. Demand paging, stack growth, copy-on-write and guard-page reporting
all require that to change, and so does any useful diagnosis of a driver bug.

**Address-space state is fixed-size and small.** `max_native_mappings` is 64
per space and `max_native_physical_mappings` is 256 globally, as
`std::array` members. Fine for a boot image with three spaces; not a structure
a running system creates spaces in.

## The decision: the kernel does not allocate

**Cookie's kernel will have no dynamic memory allocator.** Every kernel object
is derived from memory that a process already holds authority over, at a size
fixed at compile time, and the authority to derive it is a capability.

The consequence that justifies it: **no system call can fail because the kernel
is out of memory, and no process's memory consumption can affect another's.**
Both become structural rather than accounted. There is no kernel free pool to
exhaust, so there is no allocation to race, and no shared allocator whose
timing or fragmentation carries information between processes.

The alternative - a kernel heap, which is what almost everything does - has
costs that are easy to underweight at this stage and impossible to remove
later:

- Every user-triggerable allocation becomes a denial-of-service channel. A
  process that can make the kernel allocate can make the kernel fail, and the
  failure lands on whoever allocates next rather than on the process that
  caused it.
- A shared allocator is a cross-process side channel. Allocation timing and
  address reuse carry information between processes that share nothing else.
- Accounting becomes a subsystem. Deciding *whose* quota a kernel allocation
  belongs to is a question with no clean answer once objects outlive the call
  that created them, and every OS that has tried has grown a memory-cgroup-
  shaped thing to answer it.

The honest cost of the decision Cookie is making instead: userland must run an
explicit memory manager, and the API is harder to use than `mmap`. A process
cannot ask for memory; it must already hold authority over some and choose what
to make of it. That is a real ergonomic loss, paid by the SDK in Phase 7, and
it is the right trade for a kernel whose entire case is that it is small enough
to audit.

### What makes this Cookie's rather than a borrowed model

The idea that kernel objects should be derived from user-held memory authority
is not new - seL4 demonstrated it, and that it can be verified. Cookie should
not copy the mechanism, because Cookie is not starting from the same place.

seL4 introduces Untyped memory as a distinct primitive: a new object kind, a
new retype operation, and a capability derivation tree to track it. Cookie
already has the enforcement point. `NativePhysicalLedger` is consulted on every
map, already records a physical range's permissions and owning space, and
already refuses a cross-space alias that would break W^X. **The memory
capability is one more field on a structure the kernel already trusts and
already walks**, not a new object graph beside it: the ledger stops answering
only "is this alias safe" and starts also answering "who is authorized to map
this range at all."

That difference is worth the care. A separate derivation tree is a second
source of truth about physical memory, and the two can disagree - which is a
class of bug Cookie can decline to have. It also keeps the line count honest:
extending a structure the kernel already pays for is cheaper than adding one,
and M7.10's ceiling is the constraint this milestone is most likely to strain.

## Design: the four pieces

### 1. Physical memory authority

The ledger gains an owner and a state per physical range, and a small set of
capability-gated operations over it. A range can be granted, split, and
reclaimed; a range that is the backing store of a kernel object cannot be
mapped writable by anyone, which is the invariant that makes deriving objects
from user memory safe rather than merely convenient.

Boot seeds this from `plan_early_boot_memory`'s existing inventory walk - the
discovery code already enumerates usable RAM and already excludes the protected
ranges. The first process to hold authority over all unreserved memory is
whatever Phase 3's init becomes; until then the boot routine holds it.

### 2. Address-space lifecycle

Create, map, unmap, destroy, as capability-gated calls rather than boot-routine
sequences. `machine_release_address_space` gets its real implementation, gated
on the epoch authority proving no CPU is executing in the space - the condition
its own comment names, now checkable.

Page-table pages come from the caller's memory authority, not from a kernel
arena, which is what makes address-space creation possible at all after boot:
the kernel does not need a pool because the caller supplies the pages.
Intermediate-table lifetime, which `EarlyStage1Builder` explicitly punts on,
becomes tractable for the same reason - the pages have an owner who can be told
when they are free.

### 3. The fault path

`ESR_EL1` and `FAR_EL1` decoded, the fault classified, and the result delivered
to a userland pager over the IPC machinery M7.6a/M7.8 already built - the same
capability-checked, generation-bound path a driver's interrupt delivery takes.
The kernel decides *what happened*; it does not decide what to do about it.

An unresolvable fault - no pager, or a pager that refuses - kills the faulting
thread, using `destroy_thread`'s existing teardown. It does not halt the
machine. The current behaviour, halting, is correct only while there is nothing
that could possibly respond.

### 4. Layout policy stays in userland

The kernel exposes primitives to manipulate translation structures. Where a
process's regions go, what is lazily backed, and what is shared are userland
decisions. This is M7.0's argument for keeping transport out of the kernel,
applied to layout, and it is what keeps this milestone from being the one that
ends the line-count claim.

## Threat model (additions to M7.1's and M7.4's)

- **A process that exhausts its own memory authority must not degrade any
  other.** This is the property the no-kernel-heap decision buys; the gate is
  that there is no shared pool for it to exhaust.
- **A freed page must not carry data to its next holder.** Reclamation zeroes,
  and the zeroing is the kernel's, not the recipient's - a page whose contents
  the recipient must be trusted to ignore is not reclaimed, it is leaked.
- **A stale mapping must not survive teardown.** Unmap is not complete until
  TLBI has retired it on every CPU that could have cached it. Cookie is
  single-CPU today; the interface must not assume that, because the fix after
  the fact is a rewrite.
- **Page-table memory must never be writable by the process whose translation
  it controls.** A process that can write its own page tables has no address
  space. The ledger's existing cross-space check does not currently express
  this, and must.
- **A fault handler is an attack surface reached without a syscall.** Same
  category as M7.9's interrupt delivery, and it inherits the same rule: the
  capability check happens before any user code is made runnable.

## Exit criteria

- A process is created after boot, from memory authority held by another
  process, with no boot-time plan involved.
- It runs, takes a translation fault, has the fault resolved by a userland
  pager, and continues.
- It exits, and every page it held - including its page tables - is reclaimed,
  zeroed, and observably reused by a later process.
- A stale reference to the destroyed space fails closed, proven the way M7.8
  proved it for capabilities: with the same error a wrong reference gets, not a
  distinguishing one.
- All of it under `kernel-arm64-native`, not only on the host. Host tests
  prove the logic; only QEMU proves the page tables.
- M7.10 reports the raise in the same diff, with its justification.

## Open questions

**How much line count this costs, and whether that is acceptable.** `core` is
capped at 3,419 and is already 5.7x QNX's microkernel. A VM subsystem is the
single largest thing left to add, and it is entirely plausible this milestone
pushes `core` past 4,000. The M7.0 claim - that the kernel is small enough to
audit affordably - is the project's whole argument, and this is the milestone
most likely to break it. Two responses are possible and the choice is not made:
accept the raise and record the new distance honestly, or treat the ceiling as
binding and spend the milestone finding what to remove. **This should be
decided before the code is written, not after the gate goes red.**

**Whether page-table pages need reference counts.** Intermediate tables are
shared by construction. Refcounting them is the obvious answer and it is also
a per-page mutable kernel data structure, which is exactly the kind of thing
this milestone is otherwise avoiding. An alternative - making table lifetime
follow the owning region's, accepting some waste - has not been costed.

**Whether the ledger's fixed 256 entries survive.** Extending it to be the
physical authority for a running system probably means it stops being a flat
array. That is a structural change to code every mapping already depends on,
and it should be sized before it is started.

**Single-CPU assumptions.** Everything here is written for one CPU because
that is what Cookie boots. TLB shootdown across cores is not a feature to add
later; it is an interface shape to get right now.
