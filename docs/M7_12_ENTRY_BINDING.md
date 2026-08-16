# M7.12 — Where a thread is allowed to begin

`docs/M7_12_FIRST_PROGRAM.md` decides what a program *is*: a fixed-layout image,
covered by a content digest, loaded by userland into an address space the kernel
never parses. This document decides the question that immediately follows and
that the milestone document does not answer — **who chooses the address at which
a new thread starts executing**.

It is a small question with a large answer, and it has to be settled before
`thread_create` has a caller, because every later decision inherits it.

## The default answer, and why Cookie does not take it

Every system Cookie has as a reference lets the creator choose. POSIX
`clone`/`pthread_create` takes a function pointer. seL4 gives the holder of a TCB
capability `seL4_TCB_WriteRegisters`, which sets PC, SP and PSTATE to whatever
the caller likes. L4 variants are the same shape. It is the obvious design: the
kernel does not care what an address means, so it takes one.

The reason it is obvious is that in those systems the entity creating a thread
and the entity that wrote the code are assumed to be the same. That assumption is
exactly what Cookie's own decisions have already broken.

`docs/M7_12_FIRST_PROGRAM.md` says every program after the first is loaded by
userland out of the M1 package machinery — so the loader is one principal and the
image is another, signed by a lineage the loader does not control and cannot
forge. `docs/M7_11_MEMORY.md` says the process manager holds the address space as
a capability. Put those together and the loader is a principal that holds a space
containing code it did not write and is not trusted to have written.

**If the entry point is an argument to `thread_create`, that principal chooses
where signed code begins.** It can enter a program past its own initialisation,
past an argument check, at a mid-instruction offset, or at any byte of any
mapped page. The content digest still verifies — the bytes are the signed bytes.
What ran is not what the image said should run, and nothing in the chain
notices, because the chain measures content and the attack is about entry.

That is a confused-deputy gap opened by a design decision Cookie has already
made, and it is cheaper to close now than to describe later.

## Decision: the entry point is a property of the address space, fixed at seal

Cookie already has the mechanism and was using it for one job.
`SealedTranslationRoot` exists to separate **construction authority** from
**execution authority**: a builder may be mutated while a space is assembled, and
only the one-way transition into sealed state mints a token that can be
executed. The transition is irreversible and only the architecture sealer can
perform it.

The entry point is bound in that same transition. A sealed root carries the root
page *and* the one virtual address at which execution in that space may begin.
Neither can be changed afterwards, because there is no path back out of sealed
state, and neither can be supplied separately, because `SealedTranslationRoot`
has no public constructor.

`thread_create` therefore takes **no entry point**. It names a space and supplies
a stack. The kernel reads the entry out of the space's sealed root.

### Why the stack is different, and is still the caller's

The asymmetry is deliberate and is the whole of the rule: **the caller may not
choose where code begins; it may choose where data lives.**

A stack pointer selects data in a space the caller already controls completely —
it holds the space, so it can map, unmap and write anywhere in it via `map`. A
stack pointer it could not pass as an argument it could arrange by mapping.
Refusing the argument would remove nothing and would only make the caller do the
same thing in two steps.

A program counter is not like that. It selects *code*, and the code in the space
may have arrived from somewhere the caller is not trusted to speak for. There is
no equivalent "it could do it anyway": mapping a page does not let the caller
decide which byte of it a thread enters at.

So the boundary this draws is not "arguments are dangerous". It is that entry is
the one register whose value is a claim about somebody else's code.

### What this costs

It costs the ability to start two threads of the same process at different
functions. Today that costs nothing, because `ProcessTranslationTable::bind`
already refuses a second thread in the same address space
(`duplicate_epoch`) — Cookie has exactly one thread per space and this milestone
does not change that.

When that restriction is lifted, the entry stays one per space and later threads
reach their own function through a userland trampoline that dispatches on an
argument. That is written down here rather than discovered then, because the
tempting fix at that point will be to add an entry argument to `thread_create`
"just for additional threads", which reopens exactly this hole for exactly the
principal it was closed against.

## Decision: the kernel chooses the thread identifier, and never reuses one

A caller that names the thread identifier it wants learns something from the
refusal. `Rendezvous::create_thread` answers `thread_exists`, which is a probe:
a process holding `process_control` could enumerate every live thread on the
machine by asking for identifiers until one is refused. That is the same class of
disclosure `docs/M7_11_FAULT_PRIVACY.md` refused for faulting addresses, arriving
through a different door.

So `thread_create` does not take an identifier. It returns one.

Identifiers are drawn from a counter that only increases and are **never
reused within a boot**. This is not tidiness. Identifier reuse is the mechanism
behind the whole POSIX pid-reuse family of defects — a reference held across the
death of its target silently comes to name a different, live target, and the
holder cannot tell. Cookie already refuses this for address spaces, by folding a
generation into the object identifier so a stale capability simply does not name
the successor. Threads get the same property by the cheaper route available to
them: nothing is ever the successor.

Running out is an ordinary error the caller receives, never a wrap and never a
kernel failure — the same rule the no-allocator decision established for every
other exhaustible kernel resource.

Boot's own hand-made threads keep identifiers below `first_admitted_thread`, so
the counter cannot collide with them; `create_thread`'s duplicate refusal is the
backstop if that ever stops being true.

## Decision: an admitted thread is not runnable until it has architectural state

The kernel half of admission creates the thread, binds it to the space's
translation identity, and leaves it **not runnable**. The machine layer makes it
runnable after it has admitted an exception frame for it.

This ordering is the fail-safe one and the opposite ordering is not merely
untidy. A thread that is runnable before its frame exists is a thread the
scheduler may select, and selecting it means restoring architectural state that
was never written — under preemption that is not a hypothetical window, it is
whatever the timer decides. Making the last step "it may now run" means every
partial failure leaves a thread that cannot be chosen rather than one that can be
chosen and cannot be resumed.

## Decision: admission is its own right

`address_space_right_admit` is a third bit beside `hold` and `destroy` rather
than being folded into either.

Holding a space is what a pager needs — it services faults in spaces it does not
own and must be able to name them. Injecting a thread into a space is a
completely different authority, and a pager that had it could run code inside
every process it pages for. Destroying a space is different again. All three are
routinely held by different principals, so they are three bits.

A space's creator receives `hold | destroy | admit`, because it built the space
and there is no one else to hold the authority yet. Everything narrower is a
derived capability with bits removed, which the capability model already does.

## Decision: an admitted thread inherits its creator's priority

`thread_create` takes no priority argument. The new thread runs at exactly the
priority of the thread that created it.

A caller-supplied priority is a scheduling-escalation lever: a process could
create threads more urgent than itself and, with `docs/M7_1_SCHEDULER.md`'s
deadline scheduling, more urgent than anything it should be able to outrank.
Bounding the argument by the creator's priority would work, but nothing needs the
argument yet, and this project's rule is that a parameter with no caller is not
built — see the deliberately unwritten `split` over `MemoryGrantAuthority`.

When something does need it, the bounded form is the one to add: at most the
creator's priority, refused rather than clamped.

## What this does not decide

- **Whether the entry is checked against what is mapped.** The kernel does not
  verify that the entry address is executable in the space, and should not: it
  would be a second, weaker copy of the check the fault path already makes, and
  a thread entering an unmapped or non-executable address takes an instruction
  abort and dies through the path `docs/M7_11_MEMORY.md` already built. One
  enforcing check beats two disagreeing ones.
- **Who declares the entry.** Today the builder of the space passes it to the
  sealer. Once images exist, the entry comes out of the image header and the
  loader passes it through — at which point the digest covers it, and the
  property this document is defending becomes end-to-end rather than local.
  That is M7.12's loader work and is where this document stops.
- **Multiple threads per address space.** Named above as the thing that will
  press on this decision, and left to whichever milestone needs it.
