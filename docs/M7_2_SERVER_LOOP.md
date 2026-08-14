# M7.2 — Servers do not multiplex

**Status: decision, with one kernel gap named.** No code yet. This unblocks
step 1 of `docs/M7_2_DELINUX.md`'s migration order, which could not start.

## Why step 1 was blocked

`docs/M7_2_DELINUX.md` orders the migration IPC-first and says the Cookie
Kernel already provides what the Linux IPC layer rests on:

> The Cookie Kernel provides all three natively: the rendezvous in M7.1a
> preserves message boundaries by construction, `capability_grant` replaces
> descriptor passing, and the sender's identity is known to the kernel rather
> than asserted in a control message.

All three are true. They are also not the whole of what a server does, and the
missing fourth is the one that decides the shape of every service in the tree.

**Every Cookie service main loop is built on `poll()` over a set of
descriptors.** Measured, not assumed — `system/services/compositor/src/main.cpp`,
`accessibility/src/main.cpp`, `echo/main.cpp`, `keys/src/host_ci_main.cpp`,
`shell/src/main.cpp`, plus bounded-wait uses in `core/osapp/src/bootstrap.cpp`,
`core/osservice/src/bootstrap.cpp`, `core/osservice/src/identity.cpp`,
`system/app_manager/src/runtime_session.cpp`.

**The Cookie Kernel has no multi-endpoint wait.** `IpcReceiveSyscall` carries
exactly one `endpoint_capability`, and `KernelCall::receive` takes one endpoint.
There is no `poll` equivalent and no set object.

So on today's ABI, not one service main loop in the tree can be expressed. That
is the actual blocker for step 1, and it is a kernel-shaped gap that the
migration map did not name.

## The decision: delete the multiplexing, do not port it

The obvious repair is to give the kernel a readiness-set call — an epoll, a
kqueue, a `WaitForMultipleObjects`. **Cookie will not have one**, for two
reasons that are independent and each sufficient.

**It is dynamic kernel state.** A readiness set is a per-server, mutable,
caller-sized kernel object holding N registrations. `docs/M7_11_MEMORY.md`
decided the kernel has no dynamic allocator, and that decision was made
precisely so it could not be eroded one defensible subsystem at a time. A wait
set is exactly the kind of object that erodes it.

**It is a cross-client activity channel.** A readiness list tells a server which
of its clients is active, in what order, at what rate — and a server is not
always the right party to learn that. This is the same category of disclosure as
`docs/M7_11_FAULT_PRIVACY.md`'s fault trace: a mechanism whose function is
scheduling but whose side effect is a per-peer activity log. Adding the
mechanism and then trying to shape what it reveals is strictly harder than not
having it.

### What replaces it

**Many clients hold send-rights to one endpoint; the server does one receive.**
The kernel already queues senders on an endpoint, and — the part that makes this
work — **already attests who sent**:

```cpp
struct IpcReceived final {
    ThreadId caller {invalid_thread};
    IpcReplySeal reply {};   // carries reply.caller_address_space
    IpcEnvelope request {};
};
```

`IpcReceived::caller` and `IpcReplySeal::caller_address_space` are supplied by
the kernel, not asserted by the sender. That is the same property
`SCM_CREDENTIALS` gives the Linux path and the same property every ENML service
already builds identity on — so the trusted-identity invariants in `AGENTS.md`
survive the migration unchanged, which is the thing that could most easily have
broken.

The capability-system literature solves this with a *badge*: a value the minter
stamps on the send capability, which the server reads to tell clients apart.
Cookie does not need one and should not add one. A badge is chosen by whoever
minted the capability; Cookie's `caller_address_space` is derived by the kernel
from the execution authority that actually made the call. **Kernel-attested
identity strictly dominates a mint-assigned tag**, and Cookie already had it
because M7.8 bound capabilities to execution authority rather than to a thread
id alone. That work paid for this.

### What this costs, and who pays

The migration is therefore **not** a mechanical substitution of one wait call for
another. Every service main loop changes shape: from "poll a descriptor table,
dispatch by which descriptor was ready" to "receive on my endpoint, dispatch by
who called". `docs/M7_2_DELINUX.md` says of the supervisor that "the restart
logic is unchanged in shape" — that holds for supervision and does **not** hold
for servers, and this document is the correction.

The change is a simplification in every case examined: the descriptor table, the
capacity bookkeeping around it, and the `nfds`/policy-capacity coupling called
out in `system/services/keys/src/service.cpp` all disappear. The invariant in
`AGENTS.md` that "poll/epoll work should track live resources, not maximum table
size" stops being a rule to follow and becomes a rule with nothing to apply to.

## The one gap that is real: bounded receive

Collapsing the descriptor table removes the need to wait on *many* things. It
does not remove the need to wait with a *deadline*, and the tree already
depends on that: `core/osservice/src/identity.cpp`, `core/osapp/src/bootstrap.cpp`
and `core/osservice/src/bootstrap.cpp` all pass a timeout to `poll()`, and
`system/app_manager/src/runtime_session.cpp` and
`system/services/compositor/src/service.cpp` use a zero timeout to poll without
blocking.

`KernelCall::receive` blocks indefinitely. There is no way to express "wait for
a message, but not past this deadline", so a service cannot bound its own wait
and a non-blocking check cannot be written at all.

The scheduler already has deadline authority from M7.5h
(`scheduler_deadline.hpp`, `machine_set_timer`), so the mechanism exists and
needs an ABI surface rather than an implementation. Two constraints on its
design, recorded before it was written:

- **A deadline is not a timer object.** It is an argument to receive, not a
  kernel object a caller creates — same reason there is no wait set.
- **Expiry must reveal only that the deadline passed** — never anything about
  who else was queued, how many senders were waiting, or how nearly a message
  arrived. *(Corrected: this constraint was first written as "expiry must be
  indistinguishable from an empty queue", which is not achievable and not the
  point. A caller that gets control back after asking for a bound necessarily
  learns the bound expired. What must not leak is the state of other senders.)*

### Landed: the register contract

`KernelCall::receive` now takes a **relative nanosecond deadline in x2**, zero
meaning no deadline. Relative rather than absolute so the caller needs no clock —
no time syscall to add and no shared time page to trust. Zero means unbounded
because that is what every existing caller already passes, so the boot proof and
the existing EL0 program are unaffected by the ABI change.

There is deliberately **no zero-wait encoding**. A cheap "check without
blocking" primitive exists to drive busy-poll loops, and in the one-endpoint
model above a server has no reason to need one.

**The AArch64 path refuses a non-zero deadline** with
`ipc_syscall_errors::deadline_unsupported` until the timer wiring lands.

### Landed: the continuation side

`IpcReceiveContinuation` now carries an **absolute** deadline — the ABI's
relative value converted once, at arm time. Absolute is what makes it immune to
being silently extended: a relative value re-based on each wakeup drifts, and
drift in a bound is indistinguishable from not having one.

`earliest_receive_deadline()` reports the soonest across all armed receivers, so
a tickless scheduler arms **one** timer for the whole table rather than one per
waiter. `take_expired_receive(now)` removes exactly one expired waiter,
**breaking ties on the lowest `ThreadId`** — two waiters that expire in the same
instant must not learn their relative order from where the table happened to
place them — and reports only that waiter's own continuation, never how many
others were queued.

What was deliberately *not* built: no timer object, no wait set, no per-waiter
callback, no priority queue. The table is the same fixed array scanned linearly,
because `max_threads` is small and a heap would be mutable kernel structure
bought for an asymptotic improvement nothing here needs.

### Landed: one armed time, one owner of it

`narrow_decision_timer` clamps a scheduling `Decision`'s timer to the soonest
bounded receive before the deadline authority prepares it. Narrowing only — it
can bring a wakeup forward, never postpone or remove one, so an IPC deadline
cannot weaken a scheduling guarantee.

The ordering is the whole point and the obvious alternative is wrong.
`SchedulerDeadlineAuthority::accept_interrupt` rejects a delivered deadline
whose absolute time differs from its own, and rejects an interrupt arriving
before that time as `early`. Clamping the `ExecutionUniversePlan` *after* commit
— where a reader naturally reaches, since that is what calls
`machine_set_timer` — arms hardware the authority does not know about, and the
interrupt it produces is refused by the authority's own staleness check. The
receive deadline would simply never fire, and nothing would say so. Clamping the
`Decision` keeps one armed time and one owner of it.

### Landed: the wake reason

`WakeReason::deadline_expired` and `Rendezvous::expire_receive`. The transition
already existed — `cancel_receive` moves a receive-blocked thread to ready — so
this buys a fifth reason and a second entry point rather than new machinery.

Distinct from `endpoint_retired` deliberately: that says the endpoint is gone
and retrying is pointless, this says nothing arrived in time and retrying is the
normal thing to do. Collapsing them would make a service treat a routine timeout
as a dead peer. A separate function rather than a reason parameter for the same
class of reason — the callers differ (endpoint retirement; the kernel's own
timer) and a shared parameter is a way for a later caller to pass the wrong one.

Both keep the same guard: only a thread genuinely receive-blocked with no
partner can be moved, so this is not an arbitrary thread-wakeup primitive.

### Complete: the ABI no longer refuses

`KernelCall::receive` honours a deadline. The receive syscall converts relative
to absolute against `machine_monotonic_nanoseconds`, **saturating** — a relative
deadline large enough to overflow the clock is a request to wait effectively
forever, and a wrapped sum is a small absolute value that reads as already
expired, the precise opposite. The timer handler drains expired waiters in a
loop *before* it chooses, so a waiter whose deadline has passed is runnable in
that decision rather than a slice later, and several deadlines falling inside
one timer grain do not each need their own interrupt. Both reschedule sites pass
the earliest deadline, because the reschedule following a blocking receive is
what arms the timer at all — on an otherwise idle system there is no later tick
to fall back on.

**A timed-out receive returns the same shape a successful one does**, with the
fields that would identify a sender left empty: transaction zero, size zero.
There was no sender. Inventing a distinguishable encoding would leak that the
wait ended for a reason other than a message, which is the disclosure this
document's own residue section is trying to keep bounded.

**The one thing that had no precedent** is that an expiry is consumed by
reading it. Every other wake in this kernel is consumed by taking its payload —
a reply is taken, a delivered message is taken — but an expiry has nothing to
take, because the whole point is that no message arrived.
`Rendezvous::take_deadline_expiry` therefore clears as it reports. Without that,
the completion path reports the same timeout on every switch back to the thread
and overwrites its registers each time.

## Order after this

`docs/M7_2_DELINUX.md`'s order stands and is unchanged: IPC, then process and
supervision, then display buffers, then sandboxing last. What changes is that
step 1 now has a defined shape and one named prerequisite instead of an
assumption that the primitives were already sufficient.

**Zero on the coupling gate remains necessary and not sufficient.** Cookie is
off Linux when the tree builds and its gates pass with no Linux headers
anywhere, on the emulated reference platform, with the Cookie Kernel underneath.
18 of 18 permitted files still depend on Linux today.
