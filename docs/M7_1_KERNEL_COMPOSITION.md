# M7.1f - Four tables are not a kernel

M7.1a-e built the four responsibilities from `docs/M7_0_KERNEL.md` as separate
state machines: the rendezvous, the capability table, interrupt dispatch and the
scheduler. Each is testable without the others, and each document says why that
separation matters - two state machines that each know about the other are two
state machines neither of which can be tested alone.

That separation stays. What it left behind was a gap nobody had written down:
four individually correct tables are not a kernel. Something has to make them
agree, and nothing did.

## The two obligations

**A thread's death must reach every table.** Exiting releases the threads blocked
on it, surrenders the capabilities it held and everything derived from them,
releases its interrupt sources masked, and removes it from the run queue. All
four already existed and all four were already correct. The failure mode was
never a wrong implementation - it was a *forgotten call*, and a forgotten call
here is a capability that outlives its holder or an interrupt line nobody owns.
Neither fails loudly. So destruction is now one operation that does all four and
reports what each released, rather than four operations a caller is trusted to
remember.

Reporting the counts is not decoration. It is the same choice `exit_thread` and
`revoke` already made: return how many were released, so a caller can observe the
obligation was met instead of trusting that it was.

**The scheduler's view is recomputed, never patched.** The rendezvous decides
whether a thread is runnable and what priority it effectively runs at. The
scheduler needs both and holds a cache. An incrementally updated cache drifts the
first time a path is missed, and both directions of drift are serious: a thread
ready in the rendezvous but not runnable in the scheduler never runs again, and
the reverse puts a blocked thread on the processor.

So after any operation that can change either fact, every live thread's entry is
recomputed from the rendezvous. This is M7.1b's argument for inherited priority -
recomputed rather than adjusted, because an adjustment missed once stays wrong -
applied to the place where two components must agree.

The cost is a bounded scan of the thread table per system call. That is the price
of having no cache-coherency problem inside the kernel, and at a stated ceiling
of 64 threads it is cheap. It is also the kind of cost worth paying in the
component where a stale bit is unrecoverable.

## What this makes true that was not

Priority inheritance now reaches the scheduler. Before this, M7.1b computed the
inherited value correctly and nothing carried it across, so a low-priority server
handling a high-priority client would still have lost the processor to an
unrelated middle-priority thread. The mechanism existed and the effect did not.
The test asserts exactly that: a server at priority zero, serving a client at
nine, beating a bystander at five.

An interrupt now makes its driver runnable. The interrupt table concluded a
driver should run; the scheduler decides who does; neither could act on the
other's conclusion.

## What is deliberately not here

**No system call entry path.** `abi.hpp` fixes the surface and `decode_call`
validates by search rather than by indexing. Binding a decoded call to a
capability check and then to these operations is the next layer up, and it needs
the machine layer to have something to be entered *from*.

**No address spaces.** The kernel composes threads, and threads belong to address
spaces that only exist below the machine boundary. That is M7.3c.

**No policy about who may create threads.** `thread_create` is gated on
`process_control` in the ABI. Enforcing that is the entry path's job; this layer
would have to be told, and being told by a caller is not enforcement.
