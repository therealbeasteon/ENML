# M7.1d - Interrupt dispatch, and the interrupt threat model

The third of the four kernel responsibilities in `docs/M7_0_KERNEL.md`, and the
last one that is pure logic - so like the rendezvous and the capability table it
is written with no hardware in it and tested on a development host.
`core/oskernel/include/os/kernel/machine.hpp` owns masking, unmasking and
acknowledgement; this milestone decides *when* those happen and to whom an
interrupt belongs.

`docs/M6_0_DEVICE_ACCESS.md` deferred interrupt authority with the note that it
was "deliberately absent rather than half-specified; it needs its own threat
model." This document is that threat model, and the state machine is what
enforces it.

## What the references establish, and what they cost

The microkernel that carried an entire operating system in 605 lines connected a
handler **inside a user process** to an interrupt vector, and the kernel called
that handler in interrupt context. The handler had full access to its process's
address space, accumulated work - characters into a ring buffer - and woke the
owning process only when a previously defined *significant event* had occurred: a
character count, an end-of-line, a timeout. That is what let it drive
non-intelligent serial hardware at 115 Kbaud, and the authors are explicit that
external interrupt handler support was fundamental to a resource manager matching
monolithic-kernel performance.

The same reference set records the two things that approach costs.

**Driver code that can run with interrupts off can also die there.** The
microdriver work documents the case directly: a user-level driver that takes a
lock, disables interrupts and then hits a memory error leaves the kernel unable
to initiate its recovery mechanisms. The recovery path is exactly what a
restartable driver architecture is *for*, and it is the path that gets disabled.

**A system that dispatches as fast as a device asserts stops making progress.**
Receive livelock: the machine becomes fully occupied taking interrupts and never
runs the process that services them. The teaching references name it, cite the
original work on eliminating it, and draw the conclusion that polling is
sometimes better precisely because it gives the OS control of its own scheduling.

## What Cookie does instead

Cookie keeps the goal - do not wake a driver once per interrupt - and refuses
both mechanisms.

**No user code ever runs in interrupt context.** Not a handler, not a
first-level filter, nothing. Dispatch marks the source, masks it, and makes the
owning driver thread runnable. The driver is an ordinary thread doing ordinary
work under M6.0 device policy, so a defect in it is a process fault the
supervisor restarts - the property `docs/M7_4_KERNEL_HARDENING.md` says a
replaced subsystem has to have, rather than one the migration quietly spends.

**The kernel coalesces, and reports how much it coalesced.** The reason for an
in-kernel handler was to avoid a wakeup per interrupt. Counting assertions that
arrive while one is outstanding buys the same reduction: the driver wakes once
per burst and is told the burst's size, so its own significant-event logic still
works. What it does not buy is a place for driver code to execute at interrupt
time. This is the substantive difference between Cookie's answer and the
reference's, and it is a deliberate one rather than a simplification.

**A source is masked from dispatch until its driver says the device is quiet.**
At most one interrupt is in flight per source. Interrupt load is therefore
bounded by how fast the driver makes progress rather than by how fast the device
asserts, which is the structural answer to receive livelock: a device that raises
its line continuously cannot take the machine away from anyone, because it gets
exactly one dispatch and then nothing until its own driver returns. It is also
why the ABI marks `interrupt_complete` non-blocking - a driver telling the kernel
its device is quiet must never be made to wait for the privilege, or an interrupt
storm becomes a livelock inside the driver instead of a handled condition.

### The cost, stated rather than discovered

Cookie pays a thread wakeup per burst where the reference design paid one per
significant event, and a driver needing a timestamp per individual interrupt
cannot have one - it gets a count and the times of its own wakeups. Both are
accepted. A phone's interrupt sources are not a 20 MHz UART, and neither cost is
worth a place for third-party code to run with the machine's interrupts held off.

If a future device genuinely cannot be driven this way, the honest response is to
record it as a device Cookie does not support well, not to open the interrupt
path to driver code and rediscover the failure the references already wrote down.

## Choices worth stating

**One owner per source; sharing is refused.** A shared line turns
mask-until-complete into mask-until-everyone-completes, which hands every driver
on the line a denial of service against the others. Bus-level interrupt sharing
is a legacy of expansion slots; an SoC assigns its interrupts. Inheriting the
problem in order to be general would be inheriting it for nothing.

**Coalescing counts saturate rather than wrap.** A wrapped count is worse than no
count: it reads as a small number, and a driver told "one assertion" after four
billion will believe it and skip the rescan. `Service::saturated` lets a driver
tell "exactly this many" from "at least this many". Failing in the direction that
overstates work costs work; the other direction costs correctness.

**An assertion during service sends the driver round again.** Completing a source
whose device asserted while the driver was working returns it to pending rather
than unmasking it. Without that, an edge-triggered source loses the event
outright, and a level-triggered one re-raises the moment it is unmasked - the
same work with a full interrupt entry added to it.

**A spurious interrupt succeeds and is counted.** A line asserting with no driver
behind it is a hardware or configuration fault, not a caller error, and the
saturating count is the only evidence anyone will get. A source number of zero is
different: source numbers are the kernel's own namespace, so a zero is a defect
in the machine layer rather than a line that misbehaved, and it is refused.

**Source numbers are the kernel's, not the controller's.** The machine layer
translates. This is what keeps `interrupt_attach` from being a call whose meaning
changes per board, and it is why reserving zero as invalid costs no hardware line.

**A dead driver's sources are released and left masked.** Fail closed: a masked
orphan source is a dead device, an unmasked orphan is a livelock with nobody
positioned to stop it. Between a dead device and a dead system the choice is not
close. This is the same obligation the rendezvous meets by releasing everyone
blocked on a dead thread, and the capability table by surrendering what it held.

**`begin_service` is not a system call.** It is the transition the kernel
performs when it makes the driver runnable, and the count rides back on the
wakeup. Adding a call so a driver could ask for information it is about to be
handed anyway would grow the surface by one for nothing - which is precisely the
erosion `abi.hpp` exists to prevent. The surface stays at fifteen.

**The table does not consult the capability table.** Whether a caller may attach
to a source is decided above, against a capability it holds; the table enforces
*ownership* only. Two state machines that each know about the other are two state
machines neither of which can be tested alone - the same separation M7.1c keeps
between capabilities and the rendezvous.

## Threat model

**A malicious or compromised driver, holding authority over its own source.**

- *Never completes.* Its own source stays masked; its own device goes silent.
  The damage is confined to the device it was already trusted with. The
  supervisor observing a driver that stops completing is a liveness question, not
  a containment one.
- *Completes or services out of order, or repeatedly.* Refused. Every transition
  is checked against the current state rather than assumed.
- *Attacks another driver's source.* Refused: `not_owner` on service, completion
  and detach, and `source_taken` on attach. There is no operation that reaches a
  source the caller does not own.
- *Crashes.* Its sources are released and left masked, and the supervisor
  restarts it; it re-attaches and the device resumes. Recovery rather than
  reboot, which is the property the migration is being judged on.

**A malicious or malfunctioning device.**

- *Asserts continuously.* Bounded by construction. One dispatch, then masked
  until its own driver returns; further assertions become a count. This is the
  receive-livelock defence and it does not depend on the driver behaving.
- *Asserts an enormous number of times.* The count saturates and says so.
- *Asserts a line nobody owns.* Counted as spurious, wakes nobody.

**A thread with no interrupt authority.** Cannot attach - that is checked above
against a capability - and cannot service, complete or detach, because every one
of those is ownership-checked here as well. The authority check and the ownership
check are independent, so a mistake in either is not sufficient on its own.

**What is not defended, and is not claimed.**

- *Interrupt timing as a side channel.* Arrival times leak device activity to
  whoever can observe scheduling. That is M6.2 partition-ledger territory and is
  not addressed here.
- *A driver holding its own device masked forever.* A self-denial of service the
  kernel deliberately does not police, because a kernel-imposed deadline on
  completion is a policy decision that belongs to the supervisor, which can see
  whether the driver is making progress. Recorded as the supervisor's obligation
  rather than silently assumed away.
- *Board-level shared lines.* Refused rather than supported. A platform that
  wires two devices to one line cannot express that here, and should not be able
  to.
- *Priority among sources.* Which pending source a scheduler picks first is a
  scheduling decision, and this table deliberately does not make it - the same
  line the rendezvous draws when it takes the longest-waiting sender and leaves
  priority ordering to the scheduler.

## What is not decided yet

**Attaching the interrupt capability to the source.** M7.1c gives capabilities an
opaque `ObjectId`; an interrupt source is an obvious thing for one to be
authority over, and wiring that up is the composition step where the ABI's
`interrupt_control` authority becomes a capability check rather than a role.

**The first-level handler itself.** Turning a real controller's signal into a
call to `dispatch()` is machine-layer work and belongs with M7.3c, against the
ISA reference.

**Nested interrupts.** The reference kernel's first-level handler dealt with
nesting so that user handlers did not have to. Cookie has no user handlers to
protect, which removes the original reason for the machinery, but whether the
machine layer permits a higher-priority source to interrupt dispatch of a lower
one is a latency question that needs the numbers a real controller produces.
Recorded rather than assumed.
