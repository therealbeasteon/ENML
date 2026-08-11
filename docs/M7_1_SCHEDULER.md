# M7.1e - Scheduling

The last part of the first kernel responsibility in `docs/M7_0_KERNEL.md` -
address spaces and threads - and the last piece of the Cookie Kernel that can be
written as pure logic. With this, all four responsibilities exist and are tested
on a development host: message passing (M7.1a/b), capability transfer (M7.1c),
interrupt dispatch (M7.1d) and scheduling.

## What the references settle, and what they do not

The reference microkernel is fully preemptive and strictly priority-ordered, with
round-robin, FIFO and adaptive policies inside a priority. More useful than the
policy list is a remark it makes in passing about its own message-pass path:
during a send/receive/reply round trip "there are two points at which other
processes can be scheduled, rather than only at system timer intervals."
Scheduling decisions ride on work that was already happening. That is the shape
Cookie takes.

The teaching references supply the failure mode a scheduler has to be designed
against. A scheduler that restores a thread's standing whenever it gives up the
processor can be **gamed**: run for almost a whole slice, relinquish
voluntarily, keep your position, repeat - "when done right (e.g., by running for
99% of a time slice before relinquishing the CPU), a job could nearly monopolize
the CPU." The fix they give is to account for the total time a thread has run at
a level regardless of how many times it let go. They pair it with a **periodic
priority boost** so that long-running work does not starve.

Cookie takes the accounting rule and refuses the boost, for a reason that is
ENML's rather than the literature's.

## The constraint that is ENML's own

Cookie measures idle wakeups and gates on the number. M4.7 found `system.storage`
and `system.keys` each waking roughly a hundred times a second while completely
idle, and both now measure zero on both architectures. That gate is why priority
7 in `PROJECT_VISION.md` - low idle activity and power efficiency - is a measured
claim rather than an aspiration.

A periodic priority boost is a periodic timer. A scheduler with a tick would
spend the entire idle-wakeup budget before doing any work, and it would do so in
the one component nothing above can compensate for. So the boost is not
available, and neither is anything else that needs a heartbeat.

## The four rules

**Time is charged, never refunded.** A slice is consumed by nanoseconds actually
run, whatever the thread does with them. Blocking, being woken, sending and
receiving change whether a thread is *eligible*; none of them change what it has
already spent. This is the references' anti-gaming rule, and here it carries a
different weight: on a timesharing system gaming the scheduler is unfairness, on
Cookie it is a denial of service an unprivileged thread can mount against the
whole device. It belongs in the same family as the priority inversion M7.1b
closed - reachable from ordinary code, indistinguishable from load, and fixed in
the kernel because nothing above it can tell the difference.

**There is no tick.** The scheduler never asks for a periodic interrupt. Every
decision returns the single deadline at which it next needs to be consulted, and
when nothing is competing it asks for no timer at all. Every other scheduling
point is an event that was going to happen regardless: a message pass, an
interrupt, a thread exiting or being woken.

**Slices refill lazily.** Replenishment is arithmetic done at a decision point,
from monotonic time that has already passed. A refill needing its own interrupt
is a tick wearing a different name.

**Priority is never stored here as truth.** Effective priority - a thread's own,
or that of the most urgent thread waiting on it - is the rendezvous's answer, and
the scheduler caches what it was told through `update()`. Two sources of truth
for priority is exactly how inversion comes back after being fixed, and M7.1b was
explicit that the value must be recomputed rather than adjusted.

## Starvation is deliberate

A lower-priority thread does not run while a higher-priority one is runnable, and
nothing ages. This is a decision, not an omission.

The reference's periodic boost answers a timesharing workload with long batch
jobs that must eventually finish. A phone is not that. Background work that
delays a touch response is the defect users actually experience, and priority 7
asks for *less* background activity, not a mechanism that guarantees it some.

It shows up concretely in one place: when the running thread's only competition
is at a lower priority, the scheduler requests no timer. There is nothing to
preempt for, so the interrupt is never taken. A scheduler that aged would have to
take it.

The defence against a thread monopolising a high priority is not aging. It is
that priority is granted from above rather than taken - `thread_create` is gated
on `process_control` in the ABI, so choosing your own priority is already an
authority a thread has to have been given.

## Choices worth stating

**Two milliseconds, chosen against a frame rather than a reference.** At 60 Hz a
frame is about 16.6 ms, so a slice has to be well inside one for equal-priority
threads to interleave within a single frame; every halving buys that at the cost
of another context switch. Two milliseconds is eight turns per frame. This is the
kind of number a reference cannot supply, because it is a property of what Cookie
is for.

**Yield forfeits, and that is why it can be unprivileged.** The ABI's `yield` is
available to every thread. It spends the remainder of the turn rather than
banking it, so the thread goes to the back of its priority at the next decision.
A yield that preserved standing would be the references' gaming attack with a
system call in front of it.

**A clock that appears to go backwards charges nothing.** The machine layer
promises monotonic time; if it is ever wrong, an unsigned subtraction the wrong
way round bills about six hundred years and exhausts the slice instantly. Failing
to a zero charge turns a clock glitch into nothing rather than into a scheduling
fault.

**Preemption is reported, and means what it says.** A thread that blocked gave
the processor up; a thread that exited no longer exists to have been wronged.
Only a thread that was still able to run and lost it anyway is preempted.
Conflating them would make the fact useless for the one thing it is for, which is
noticing that something is being pushed off the processor.

**Admission order is round-robin order.** A newly admitted thread takes its turn
behind those already waiting rather than at the head. Both are defensible; this
one is easier to reason about and to test, and the difference is bounded by a
single slice either way.

## Threat model

**An unprivileged thread trying to take more than its turn.** Charged for what it
runs regardless of how it stops running, so relinquishing early gains nothing and
yielding costs the remainder. There is no path that restores standing.

**An unprivileged thread trying to raise its priority.** Not expressible here.
The scheduler is told priorities; it does not accept them from the thread they
apply to. Creating a thread at a priority is `process_control` authority, and
inherited priority is the rendezvous's computation over threads *actually
waiting*, which M7.1b already bounds so that a past caller donates nothing.

**A thread trying to stall a higher-priority one.** This is the priority
inversion case, and it is closed one layer down: a server runs at the priority of
the most urgent thread waiting on it, so blocking on a shared server cannot park
work that outranks you.

**What is not defended.** A high-priority thread that simply runs forever will
starve everything below it, by design - see above. Detecting that a supposedly
interactive thread is in fact a spinning one is a supervisor question, because
the supervisor can see what the thread is for and the kernel cannot. Recorded
rather than assumed away.

## What is not decided yet

**Multiprocessor.** Everything here assumes one processor. A phone SoC has
several, and the extension - per-processor run queues, affinity, and what
priority inheritance means across them - is real design work rather than a
parameter. Nothing in this milestone quietly assumes it away; the interface
returns a single decision because there is a single processor to make it for.

**Where the slice number really belongs.** Two milliseconds is defensible and
untested against a real workload. It is a constant in one place precisely so that
measuring it later changes one line.

**Idle power.** Choosing nothing to run currently means "the processor idles",
and what idling costs - which sleep state, how long it takes to leave - is a
machine-layer question that needs M7.3c and real hardware.
