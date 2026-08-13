# M7.3 - The machine layer

Everything in the Cookie Kernel above this boundary is architecture-independent
and testable on a development host: the rendezvous, priority inheritance, the
capability rules. `core/oskernel/include/os/kernel/machine.hpp` is where that
stops. Below it are register windows, page tables, exception vectors and,
unavoidably, assembly.

The boundary is defined before it is implemented, for the same reason the system
call surface was. The architecture-specific part of an operating system grows
without anyone deciding to grow it - each addition is locally reasonable, and
the result is a kernel that must be re-verified per board rather than per
change. Naming the operations makes that surface a decision.

## The two rules

**Assembly lives only behind these functions.** Not in the rendezvous, not in a
service, not inline in portable code for speed. A tree with architecture
assumptions scattered through it is one where a port means auditing everything
instead of auditing one directory.

**Nothing here makes a policy decision.** These operations do what they are
told. Which thread runs, which address space a mapping belongs to, and whether a
caller may request it are all decided above, where they can be tested without
hardware. A machine layer that chooses is a second kernel nobody is reviewing.

## Choices worth stating

**Device memory is a kind, not a hint.** Device registers must not be cached,
speculated into, or reordered around. A layer that treats the distinction as
advisory produces drivers that work until the day a compiler or a core reorders
something, and that failure is not reproducible.

**Interrupts are masked per source, never globally.** A global disable held
across more than a handful of instructions is where worst-case interrupt latency
comes from, and latency that depends on what the kernel happens to be doing is
not a bound anyone can state. The references measure this directly - single-digit
microsecond interrupt latency on hardware two decades old - and the number is
only meaningful because the kernel never holds interrupts off for long.

**Time is nanoseconds, never ticks.** A machine layer that reports ticks makes
every timeout above it board-specific, which is hardware neutrality lost at the
first line.

**Contexts and address spaces are opaque.** The portable kernel stores handles
and hands them back; it never inspects one. The moment it knows the shape of a
saved register file or a page table entry, the portability claim is gone.

## What is not decided yet

The AArch64 implementation. This milestone is the contract; the ISA reference
and the assembly guide in the reference library are what the implementation is
written against, and that is deliberately separate work - the contract can be
reviewed for whether it is the right shape without anyone reading a single
instruction encoding.

The host implementation comes first regardless, because it is what keeps the
portable kernel testable while the real one is written.
