# M7.4 - Hardening the Cookie Kernel, and replacing rather than removing

Two corrections to how this migration has been described.

## Replacement, not subtraction

`docs/M7_2_DELINUX.md` lists what each Linux dependency *becomes*, and reads as
though the goal were to stop using things. It is not. Everything Linux does for
Cookie today has to be done by Cookie - drivers, filesystems, networking, power
management, scheduling - and doing it ourselves is only worth the cost if the
result is better on the axes this project exists for.

Better is not a slogan here. It has a shape:

- **Restartable.** A Linux driver fault is a kernel fault. A Cookie driver is a
  user-space process under M6.0 device access policy: it dies, the supervisor
  restarts it, and the clients that were talking to it are released with
  `peer_exited` rather than parked forever. That property already exists in
  M7.1a and is exercised by the M2.10 fixture.
- **Capability-scoped.** A Linux driver holds whatever the kernel holds. A
  Cookie driver holds bounded MMIO windows and nothing else, cannot express port
  I/O authority at all, and has its isolation claim refused outright when the
  platform cannot back it.
- **Individually replaceable.** A resource manager can be started, stopped and
  swapped without relinking or rebooting, which turns experimenting with a
  filesystem or a network stack into an ordinary development task instead of a
  kernel project.
- **No slower.** On the measurements the references actually report: message
  passing around fourteen times a monolithic UNIX, sequential file reads five to
  eight times, pipe I/O about twice. Not despite the message-passing structure
  but because of it - multipart messages remove the copy a naive design would
  add, and interrupt handlers live inside the driver process rather than forcing
  a round trip.

So the measure of this migration is not an empty header list. It is that each
replaced subsystem is restartable, capability-scoped and no slower. A subsystem
that cannot meet those has not been replaced, it has been removed - and that is
a regression however clean the list looks.

## The kernel is not exempt from hardening

Cookie userspace is built with stack protector, stack-clash protection,
branch/CF protection, full RELRO, non-executable stack, position independence
and standard-library assertions, all verified in the produced binary rather than
assumed from flags. The kernel inherits none of it, and most does not port -
RELRO and PIE are loader concepts, and a kernel has no loader.

What is required of the Cookie Kernel instead:

- **No ambient authority.** Already true: every call outside the unprivileged
  core is gated on a capability the caller holds, so within the ABI there is no
  privilege to escalate *to*.
- **No kernel heap.** Every structure is a fixed array with a stated ceiling -
  threads, calls, arguments. Nothing to exhaust, no fragmentation, and no
  allocation-failure path to get wrong. That is a security property, not a
  performance one.
- **W^X in kernel mappings.** No page the kernel maps may be both writable and
  executable, including during boot. `MachinePermissions` has no such
  combination, which makes it unrepresentable rather than merely discouraged.
- **A guard page below every kernel stack.** In userspace stack-clash protection
  is a compiler measure; in the kernel it is a mapping decision, and an unmapped
  page turns an overflow into a fault instead of into another thread's state.
- **Bounded work per call.** No kernel call may loop an unbounded number of
  times on data a caller controls. The rendezvous already scans a fixed table;
  the rule is that this stays true, because an unbounded loop in the kernel is a
  denial of service no scheduler can preempt.
- **Constant-time where secrets pass.** `constant_time_equal` and `secure_zero`
  apply unchanged, and the kernel additionally must not branch on a capability
  value while checking one.
- **Entry-path discipline.** System call entry is the one place an attacker
  picks a number that selects code. It validates by search and rejects unknown
  values rather than indexing - already true, and the property most worth
  defending when someone later wants a jump table for speed.

The first, second and last are already properties of what exists. W^X, guard
pages and bounded work are obligations on the machine layer and on every call
added after it. They belong in review for M7.3b and M7.3c rather than in a later
hardening milestone, because a kernel hardened afterwards is one that was
written wrong first.

## M7.4b - the three obligations, enforced in the machine layer

The section above ends by saying W^X, guard pages and bounded work "belong in
review for M7.3b and M7.3c rather than in a later hardening milestone, because a
kernel hardened afterwards is one that was written wrong first." M7.3b built the
host machine layer. This is that follow-through, and two of the three turned out
to need real mechanism rather than a restated intention.

### W^X had a hole a type cannot see

`MachinePermissions` has no writable-and-executable value, so no single mapping
can break the rule. That is the good half, and it stays: a rule a type makes
unrepresentable cannot be forgotten by an implementation, and every machine layer
written later inherits it for free.

The half a type cannot see is **aliasing**. Map one physical page `read_write` at
one virtual address and `read_execute` at another, and that memory is writable and
executable at the same instant through two mappings that are individually
blameless. Nothing about either permission value is wrong. It is caught on the
*physical* range, because the virtual ranges do not overlap and never will.

Refusing all aliases would have been simpler and wrong - two writable views of one
buffer is how shared memory works, and two read-only views is how anything is
shared safely. The rule is the combination, not the aliasing.

M7.4b initially covered only aliases visible inside one address space and recorded
cross-address-space aliasing as an explicit gap. M7.4c below closes it rather than
leaving the warning as permanent documentation debt.

### A guard page is a mapping decision, so it lives at the mapping operation

`machine_map_kernel_stack` is now the only way to get a kernel stack, and it
refuses unless the page immediately below is unmapped. A stack with no room
beneath it for a guard is refused rather than mapped and described as guarded.

That alone would still be a convention, because nothing stopped a caller starting
a thread on memory it allocated itself. So `machine_prepare_context` now requires
the stack pointer to be the top of a range established that way. The guard rule
reaches the operation that would otherwise bypass it, which is the difference
between a rule that holds and a rule that holds until somebody is in a hurry.

Stacks grow downward, so the guard sits below the range and the initial stack
pointer is its top. Handing in the bottom is refused - accepting it would put the
guard page above the thread rather than below it, which is the one place it does
no good at all.

### Bounded work was already true, and is now the reason to look

Every operation in the machine layer scans a fixed table once. Nothing here loops
on a caller's number. That was already the case, and the reason to state it is
that the next person to add an operation is the one who can break it.

## M7.4c - W^X becomes a machine-wide physical-memory invariant

A per-address-space W^X check is not W^X. If process A can map physical page P
`read_write` while process B maps the same P `read_execute`, P is writable and
executable simultaneously even though neither page table contains a forbidden
permission value.

The machine seam now makes the missing authority explicit with
`MachinePhysicalLedger`. Every address space must bind to the one ledger for its
machine before any mapping can be admitted. The ledger is opaque above the
machine layer; portable kernel code does not inspect it or make policy from its
representation.

For the host implementation the ledger is fixed at 256 mapping records and each
address space remains fixed at 64. No admission allocates. `machine_map()` and
`machine_map_kernel_stack()` validate local virtual overlap, then compare the
new physical range with **all currently admitted physical mappings**. Any
writable/executable overlap is refused regardless of which address space owns
the other alias. Partial physical overlap is treated exactly like an exact alias.

The order of mutation matters. A free local slot and a free physical-ledger slot
are found before either table is modified. Failure therefore cannot leave a
mapping that exists in only one authority view. Unmap removes both records.
`machine_release_address_space()` first proves that the two tables agree, then
removes every mapping owned by that exact address-space object and detaches it.
Dropping a process is therefore also revocation of its physical mapping authority,
not merely loss of its virtual addresses.

The address-space and physical-ledger host objects are non-copyable and
non-movable. Their object identity participates in ledger ownership; allowing an
ordinary C++ copy would manufacture a second object containing mappings whose
ledger records still point at the first.

The rule deliberately allows aliases that do not create W+X: writable+writable,
execute+execute and read-only combinations remain legal. Shared memory is not a
security violation by itself. The forbidden state is a physical overlap with one
writable view and one executable view.

Kernel stacks participate in the same ledger because their backing pages are
writable. A second address space cannot map a live kernel stack backing range
executable. This closes an otherwise easy route around the guard/W^X split.

### Negative tests added for the actual failure modes

The machine contract now proves, on GCC, Clang, ASan/UBSan and native AArch64
host execution:

- an unbound address space cannot map;
- a live address space cannot silently rebind to a different ledger;
- exact and partial cross-address-space RW/RX physical aliases fail closed;
- same-permission shared mappings remain possible;
- failed admissions leave both tables unchanged;
- unmap and full address-space release remove physical authority;
- a released address space cannot map until explicitly rebound;
- kernel-stack backing cannot become executable through another address space;
- physical-ledger capacity exhaustion is bounded and leaves no partial mapping.

### Reference discipline for this hardening step

The newly supplied sandboxing material emphasizes a small, tamper-resistant
reference monitor through which every protected request passes. ENML applies the
principle here without importing its example mechanisms: the physical ledger is
the one admission authority for mappings, so there is no second per-process path
that can create an alias outside the check.

The supplied mobile key-storage/encryption and boot/update references reinforce a
broader rule already used elsewhere in ENML: a security property must follow the
resource itself rather than the name a caller uses for it. Keys remain protected
across process boundaries; boot trust follows the artifact rather than the update
channel; W^X follows the physical page rather than one virtual address space.

See `docs/REFERENCE_ADDITIONS_2026_08_11.md` for the classification of those
sources. Their mechanisms are not the Cookie Kernel design specification.

### What this still does not claim

This host ledger is a contract implementation and validation surface, not yet a
hardware page-table implementation. The future AArch64 machine layer must bind
all hardware address spaces to an equivalent machine-wide authority and satisfy
the same tests. DMA/IOMMU mappings are also a separate physical-access domain;
when DMA arrives, its executable/write interaction must be threat-modeled rather
than assumed to be covered by CPU page-table bookkeeping.

No claim of "unhackable" follows from W^X. It removes one exploit-enabling state
and makes the invariant testable. Memory-safety defects, code-reuse attacks,
malicious DMA, physical attacks, compromised firmware and logic errors remain
separate security problems with their own boundaries.
