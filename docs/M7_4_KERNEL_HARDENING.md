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
