# M7.11 - Memory as a first-class object

**Status: in progress.** `docs/ROADMAP.md` added this milestone (Phase 2b)
after a gap review found virtual memory absent from the phase list rather than
deferred within it. This document makes the decisions that have to be made
before any of it is written, because the central one - whether the kernel
allocates - cannot be revisited once subsystems depend on the answer.

Landed so far, each in its own reviewable diff:

1. **The fault is described rather than announced.** `describe_fault` decodes
   `ESR_EL1`/`FAR_EL1` into abort class, fault status, translation-table level
   and read-vs-write; `cookie_aarch64_unhandled_fault` reports it. The
   classification is what the fault path below delivers to a pager - only the
   destination changes.
2. **The ledger knows which physical ranges hold kernel state.** Piece 1 below,
   partially: reservations, their three rules, and boot declaring the
   page-table arena. Grant, split and reclaim are not written yet.
3. **The kernel's own writable state is declared too**, in the same table -
   `__cookie_data_start..__bss_end` and the kernel stack - under a second
   reservation kind that constrains writers less because TTBR0-only translation
   forces it to. See "Two kinds, and why" under piece 1.
4. **An address space can be released.** `aarch64_release_address_space` unmaps
   every mapping through the ordinary `machine_unmap` path, drops the space's
   reservations, retires the ASID and unbinds, leaving the `MachineAddressSpace`
   rebindable. The untyped `machine_release_address_space` still refuses, and
   the reason is now a statement about its contract rather than about missing
   work - see "Address-space destruction" below.
5. **Page-table pages can be donated**, which is where post-boot pages come from
   at all. `aarch64_donate_table_page` reserves the page before donating it, so
   the reservation rules refuse a donor that has not unmapped its own page
   first. The kind is `kernel_private`, not `kernel_object` - see the EL0 entry
   below for why TTBR0-only translation forces that, and what it does not give
   up.
6. **An address space can be created after boot**, in the order the circular
   dependency permits rather than the one the design implied: create, donate,
   initialize root, map, seal. See "Creation works" below.
7. **A capability can name one lifetime of a space.** `address_space_object_id`
   folds the generation into the object id, so a reference held across a
   destroy-and-recreate stops resolving on its own, with the ordinary
   not-found error rather than a distinguishing one.
8. **The kernel decides who may create and destroy one.**
   `Kernel::address_space_create` and the two-phase
   `address_space_begin_destroy`/`address_space_complete_destroy`, plus
   `AddressSpaceEpochAuthority::resolve` to recover an epoch from the identity
   a capability carries. The exit criterion about stale references is tested
   directly: a capability minted over a destroyed space does not reach the
   space that later occupies its slot.
9. **A process can create and destroy one.** Calls 7 and 8 dispatched at the
   AArch64 syscall entry, proven from EL0 under `kernel-arm64-native`.
10. **A destroyed space's pages are erased.** Every range it owned is zeroed
    before its reservation is dropped, proven by a paired non-zero-then-zero
    check rather than a one-sided one.

Cookie's kernel could, until item 6, create address spaces exactly once, at
boot, from a plan computed before any process existed. Everything M7 built on
top of that - epochs, capabilities, IPC, interrupts, preemption - is real and
tested. The memory underneath it was not: it was a boot artifact that no
running code could change. This milestone is where that stops being true, and
it is the largest single addition the kernel will take.

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

**Address-space destruction: half done.** `aarch64_release_address_space` now
unmaps every mapping through the ordinary `machine_unmap` path, drops the
space's reservations, retires the ASID and unbinds - leaving the
`MachineAddressSpace` rebindable, which is what makes reuse possible at all.

`machine_release_address_space` deliberately still returns
`machine_errors::unsupported`, and the reason changed from "not written" to a
statement about the contract: bulk release is safe only once no CPU can be
executing in the space, and that signature takes the space and nothing else, so
it cannot carry the proof. The typed entry point takes a
`RetiringAddressSpaceEpoch` - `begin_retire()` has already invalidated the
software epoch, so no thread can be scheduled into the space. Cookie is
single-CPU today and this is the interface shape that stays correct when it is
not, because the caller establishes the property rather than the machine layer
guessing it locally.

**Creation after boot: the page source exists.** `EarlyPageArena` now takes
donated pages as well as a bump range, and works with no bump range at all -
which is the post-boot shape, because nothing was planned for a space created
after any process existed. `aarch64_donate_table_page` is the enforcement point,
and the enforcement is a composition rather than a new rule: it reserves the
page before donating it, and `reserve_physical` already refuses a range some
process can still reach. A donor that has not unmapped its
own page is rejected before that page becomes a translation table it could keep
writing.

That is the reservation hole closed from the other direction, and it is worth
noticing which piece did the work: the "declaring is not retroactive blessing"
re-check, written for boot and never exercised by boot, is what catches the
donor. It was justified at the time as covering a caller that does not declare
before it maps. This is that caller.

**Creation works, and its shape is not the one the design implied.** The
sequence is `create` (bind a space to a ledger and a rootless builder) ->
`donate` at least once -> `initialize_translation_root` -> map -> seal.

It cannot be one call, and the reason is a genuine circular dependency rather
than an implementation convenience. A translation root is a page. After boot,
pages come from a caller's authority. Donating reserves through a space, so the
space must already be bound. Binding used to require a root - which is what
boot's order implies, because boot fills its arena before anything runs - and
that requirement made creating an address space depend on itself.

`bind` now accepts a rootless builder. Safe rather than lax: `install_leaf` and
`leaf_pointer` already return `address_space_unbound` when the root is zero, so
every map, unmap and query fails closed in that window, and only
`reserve_physical` works - exactly what donation needs and nothing more. The
alternative considered and rejected was letting the *creator* own the new
space's root reservation, which breaks the cycle too but puts teardown
ownership in the wrong place: `release_reservations` drops what a space owns,
and a root the creator owns would outlive the space it belonged to.

**The capability gate and the epoch now exist.** `Kernel::address_space_create`
checks a capability, acquires an epoch, and mints a capability naming that
epoch's identity; `address_space_begin_destroy`/`address_space_complete_destroy`
check a capability, resolve the space it names, and drive the two-phase
retirement the ASID lifecycle requires. None of it touches a page table - the
kernel owns which lifetimes exist and who may name them, the machine layer owns
the tables, and the `aarch64_*` calls above run between them.

`AddressSpaceEpochAuthority::resolve` was the missing piece that made
capability-driven destroy possible at all: a capability names a space by
identity, because that is what an object id can carry, while retirement needs
the epoch, which also carries an ASID. It stores nothing to close that gap,
because the ASID was never independent information - `acquire()` derives it
from the slot and `active()` already re-derives it to validate what it is
handed.

**The boot proof exists, and covers half of what it should.**
`kernel-arm64-native` gates on `COOKIE:M7.11:SPACE_CREATED`,
`..:SPACE_DESTROYED` and `..:STALE_REFUSED` - an address space created after
boot, authorized by a capability, retired in two phases with the machine
layer's release between them, and a capability over the identity it used to
have refused afterwards. The stale-reference exit criterion is met on the
machine, not only on the host.

It donates pages, builds a translation root from one of them, and maps a page
into the created space, so the sequence this milestone had to invent - create,
donate, initialize root, map, seal - is exercised on the machine rather than
described.

An earlier version of this section claimed that donating was blocked by
`forbidden_by_reservation`, and that claim was wrong in its premise. It is
left corrected rather than deleted because the mistake is instructive.
`early_identity_space` has its own ledger, not `boot_physical_ledger`, and a
reservation is checked only against the mappings of the ledger the reserving
space is bound to. The early map's writable mapping of a donated page is
therefore invisible to the reservation that donation takes, and nothing
refuses it. There was no conflict to resolve.

Relying on that is sound for the same reason the separation exists (see
`early_identity_ledger`'s declaration): the two maps are sequential rather
than concurrent, the early one is kernel-only, and it is abandoned before any
process runs - so no EL0 translation of a translation table ever exists, which
is the property the `kernel_object` reservation defends. The page the proof
maps is deliberately not one of the donated ones, because a user-accessible
translation of a live table is exactly what that reservation refuses.

**`forbidden_by_reservation` is not a global invariant**, and it reads like
one. It is scoped to a single ledger, and Cookie deliberately runs two. That
is a boundary rather than a hole, but it is the kind of boundary that is
easier to misread than to notice, as the erroneous claim above demonstrates.

**Still untested: the two-owner case the ledger rule does describe.** Nothing
calls `aarch64_donate_table_page` outside the boot proof, and its unit
coverage drives `EarlyPageArena::donate` directly with a host array and no
second space mapping those pages. The rule that a `kernel_object` reservation
and a foreign writable mapping cannot coexist *on one ledger* is therefore
asserted by no test. It should get one, precisely because the boundary above
means the boot path does not exercise it.

**A process can now create an address space and destroy it.** Calls 7 and 8
are decoded at the AArch64 syscall entry, and `kernel-arm64-native` gates on
`COOKIE:M7.11:EL0_CREATED` and `..:EL0_DESTROYED` after the M7.9 device proof
concludes. Two decisions in it are worth knowing before changing anything near
them.

The entry admits `CallAuthority::process_control` and then refuses every
`process_control` call other than these two. Widening an authority check is
not the same as implementing what it lets in: the others carrying that
authority have no dispatch, so without the explicit refusal a caller naming
one would fall through to the yield tail and get a wrong answer rather than a
refusal.

The root page the caller names is in the kernel manifest, and therefore mapped
in all three spaces. That is not tidiness. The kernel builds the created
space's tables from inside the caller's syscall, and Cookie translates through
TTBR0 only, so EL1 is executing under the caller's root at that moment - a
mapping in `boot_kernel_space` alone would not be there when it is needed. It
is also why donation reserves `kernel_private` rather than `kernel_object`:
three kernel-side writers of a page about to become a translation table is
exactly what the stronger kind forbids and what TTBR0-only translation forces.
Both tighten when M7.7's TTBR1 split lands, and the code says so where it
would need changing.

Spaces created this way live in a fixed two-slot pool. Exhaustion returns an
error to the caller rather than halting, which is the no-allocator decision
showing up where it should: running out is a condition a caller is told about
and can act on, never a kernel failure it cannot.

**Erasure is done; handing pages back is not.**
`aarch64_release_address_space` zeroes every range a destroyed space owned
before dropping its reservation, so the threat-model entry above - "a freed
page must not carry data to its next holder" - is a property rather than an
intention. What those ranges hold is translation tables, so what used to
survive a destroy was the space's entire layout.

Two orderings in it are the safety argument. Zeroing happens *before* the
reservation is dropped, because an unreserved range is mappable and one zeroed
after release could be claimed and read in between. And ranges are released one
at a time, so a failure part-way leaves the rest reserved rather than
unreserved and still carrying their contents.

The zeroing writes through a `volatile` pointer, and the boot proof reads
through one. Both are load-bearing: the write is a dead store to anything the
compiler can see, so removing it would delete the property silently and leave a
check for zeroes still passing, because a reclaimed range is usually zero
anyway. The proof therefore asserts the pages are *non-zero before* the destroy
as well as zero after - without that half it would pass on a range that was
always zero and keep passing if reclamation were deleted.

**Still missing: returning the pages.** They are erased and unreserved, but
nothing hands them back to a donor to be donated again, so a caller that
creates and destroys repeatedly runs out. Grant and split over the reservation
table are what close that, and they are the last unbuilt piece of the
physical-authority design.

**Also still missing: what creation should really be authorized by.** The
creation authority is a distinguished object, and it is a placeholder. In the
finished design the authority to create a space is the authority over the page
it is built from, since the no-allocator decision makes memory the only
authority there is and the caller already supplies that page. When memory
capabilities land, that object should disappear rather than sit beside them.

**The fault is described but not yet answerable.** This entry used to read
"there is no fault path," and half of it is now false: `describe_fault` decodes
`ESR_EL1`/`FAR_EL1` into abort class, fault status, translation level and
read-vs-write, and `cookie_aarch64_unhandled_fault` reports all of it. What is
still true is the part that matters most - the kernel can say precisely what
happened and can still do nothing about it, because a fault is reported and
then halts. Demand paging, stack growth, copy-on-write and guard-page recovery
need the description delivered to a userland pager over the IPC path, and need
an unresolvable fault to kill the faulting thread rather than the machine.
Neither exists.

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

**Landed:** the reservation half. `NativePhysicalReservation` records a physical
range that holds kernel state and the address space that owns it, and every
`map_impl` consults it: no EL0 translation of the range at all, no executable
one, and no writable one from a space that does not own it. Boot declares the
page-table arena through `aarch64_reserve_kernel_object` after the builders
attach and before the first mapping is drawn from it.

Two things about the shape are worth recording, because both were decisions
rather than defaults.

*The rule is stated once.* `forbidden_by_reservation` is shared by the map-time
check and the reserve-time re-check, which ask the same question from opposite
ends: at map time the reservation exists and the mapping is proposed, at reserve
time the mapping exists and the reservation is. Two copies of that predicate
would be two chances to disagree about what a reservation forbids.

*Declaring is not retroactive blessing.* `reserve_kernel_object` re-checks every
live mapping of the range and refuses if any of them is one it would have
rejected. Boot declares before it maps and so never exercises this, which is
exactly why it is written down: a reservation that silently accepted an existing
EL0 mapping would report protection it does not have.

**Two kinds, and why.** A reservation is `kernel_object` or `kernel_private`.
Both forbid the same two things - any EL0 translation at all, and any executable
one - and differ only in who may hold a writable kernel translation:
`kernel_object` allows exactly the owning space, `kernel_private` allows any
bound space. The page-table arena is the first; the kernel's writable image and
its stack are the second.

That second kind is a constraint, not a preference, and it is worth stating
because a reader will otherwise read it as the rule being quietly relaxed.
**Cookie translates through TTBR0 only.** EL1 executes under whichever process
root is installed, so `boot_kernel`'s capability table, `boot_physical_ledger`,
the epoch authority and every saved exception frame have to be writable in
*every* address space or the kernel stops running the moment a process is
scheduled. Owner-write-only is unachievable for them until M7.7 splits the
kernel domain into TTBR1 - which is exactly what
`aarch64_kernel_translation_domain.hpp` is a reviewed contract for, and which
this makes a concrete reason to want rather than a tidiness argument.

What the weaker kind still buys is the part that was actually missing: **no EL0
translation of the kernel's own state, and nothing refused one before.** The
pre-existing cross-space check sees a writable kernel mapping and a proposed
writable user mapping of the same range, finds no executable one among them, and
has no violation to report. A process holding that mapping reads other
processes' saved register state at best and rewrites the authority tables at
worst.

**Deliberately not covered:** `.text` and `.rodata`. Text cannot take either
kind, because both forbid executable mappings and text is executable by
definition; an EL0 alias of it is a gadget-discovery convenience rather than a
memory-safety hole, and pretending the reservation table addresses it would be
worse than saying it does not. `.rodata` could take `kernel_private` and was
left out to keep this increment to the ranges whose exposure is a compromise
rather than a disclosure.

**Not landed:** grant, split and reclaim, and the owner being a process rather
than an address space. The reservation table is a fixed 16 entries, which is
enough for boot and is not a structure a running system creates spaces in - the
same open question the flat 256-entry mapping array already has, and it should
be answered for both at once rather than twice.

The invariant this closes was already named in the threat model below and was
genuinely absent from the code: the pre-existing cross-space check answers only
whether two mappings disagree about W^X, so *two writable* translations of the
page-table arena - one the kernel's, one a process's - was a combination it had
no reason to reject.

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

**Amended: the pager is never told the faulting address.** Taken literally, the
paragraph above builds a controlled-channel attack into the design - a userland
pager that sees faulting addresses and can revoke access to be told again
recovers a process's secret-dependent control flow, which is the published
result against SGX enclaves at exactly this granularity. Cookie's pager is a
userland service and therefore untrusted by construction, so this is not a
hypothetical mis-trust; it is the default arrangement.

`docs/M7_11_FAULT_PRIVACY.md` is the decision and `FaultRegionTable` is the
mechanism: a pager learns *which declared region* needs backing, once per
lifecycle transition, and holds no unmap authority with which to ask again.
Regions declared `sealed` are never reported at all. What the kernel decides is
unchanged; what it is willing to *say* is now a separate, deliberate question.

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
  space. ~~The ledger's existing cross-space check does not currently express
  this, and must.~~ **Closed** by the reservation table in piece 1, which goes
  further than the wording here asked for: no EL0 translation of a reserved
  range at all, writable or not, because one that can *read* the page tables
  learns the physical layout of every other process.
- **A fault handler is an attack surface reached without a syscall.** Same
  category as M7.9's interrupt delivery, and it inherits the same rule: the
  capability check happens before any user code is made runnable.
- **A fault report is itself a disclosure.** The pager is untrusted, so what the
  kernel tells it about a fault is a channel and must be budgeted like one. See
  `docs/M7_11_FAULT_PRIVACY.md`; the rule it adds is that a secret must never
  influence which page faults.

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

Still open, but no longer without evidence. Three increments have landed and
`core` has not moved at all: the fault decoder went to `machine` and `entry`,
and physical memory authority went to `machine` as a field and two loops on a
structure the kernel already walks. Total is 8,823, up 1.3% across all three -
and the third cost 13 machine lines only because the second had already put the
table there, which is the compounding this shape is supposed to produce. That
is a real data point and it is not proof - the two pieces that will actually
press on `core` are address-space lifecycle and delivering faults to a userland
pager, and neither is written. What it does suggest is which answer is
available: if each piece can land as an extension of an existing enforcement
point rather than as a new object graph beside it, the ceiling may be closer to
binding-without-pain than the paragraph above assumed. That is a reason to keep
choosing that shape deliberately, not a reason to consider the question settled.

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
