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
blameless. Nothing about either permission value is wrong. This is a documented
way real kernels have lost W^X while every permission looked correct, and it is
caught on the *physical* range, because the virtual ranges do not overlap and
never will.

Refusing all aliases would have been simpler and wrong - two writable views of one
buffer is how shared memory works, and two read-only views is how anything is
shared safely. The rule is the combination, not the aliasing.

Cross-address-space aliasing is **not** covered: a page mapped writable in one
address space and executable in another is invisible to a check that sees one
space at a time. That needs a physical-memory ledger the kernel does not yet have,
and it is recorded here rather than discovered later.

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

### What this does not do

It does not harden the AArch64 layer, which does not exist. The value of putting
these rules in the contract and in the host implementation is that M7.3c has to
satisfy the same tests - the guard page and the alias check are written against
the contract, not against the host.
