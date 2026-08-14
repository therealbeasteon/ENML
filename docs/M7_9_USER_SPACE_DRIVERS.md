# M7.9 - User-space driver framework

`docs/ROADMAP.md` names this milestone in one line: "Interrupt handlers inside
driver processes, connected to a vector by a kernel call, under M6.0 device
access policy. This is the piece that makes 'no drivers in the kernel' true
rather than aspirational." Every status this project has published about M7.9
says the same thing: no source, no branch, no document.

That is true of M7.9's own name. It is not true of the design it depends on.
`docs/M7_1_INTERRUPT.md` already wrote the threat model. `interrupt.hpp`
already implements the state machine and has since M7.1 - host-tested,
composed into `Kernel`, exercised by `kernel_interrupt_test` and
`kernel_composition_test` on every push. The ABI already reserves
`interrupt_attach` (11), `interrupt_detach` (12) and `interrupt_complete` (13)
as `KernelCall` values. What M7.9 actually is, once the tree is read rather
than the roadmap line, is the last mile of a milestone that was mostly built
under a different name: closing the four gaps M7.1 and M6.0 each explicitly
deferred, and proving the whole chain end to end under QEMU the way M7.5i
proved two-process preemption.

Getting this wrong costs more than most gaps in this tree. A driver process is
the one that receives control based on a hardware event rather than a
syscall a thread chose to make, and the capability check that gates it is the
only thing standing between "a device raised its line" and "arbitrary
unprivileged code started running against it."

## What already exists

**The state machine.** `core/oskernel/include/os/kernel/interrupt.hpp` and its
`.cpp` implement attach, detach, dispatch, `begin_service`, `end_service`, and
per-thread cleanup on death, exactly as `docs/M7_1_INTERRUPT.md` specifies:
one owner per source, no sharing, mask-until-driver-completes as the
structural defence against receive livelock, saturating coalesced counts, no
user code ever runs in interrupt context. This is done. It is not being
redesigned here.

**Composition into `Kernel`.** `core/oskernel/include/os/kernel/kernel.hpp`
already holds an `InterruptTable interrupts_` member, exposes it via
`interrupts()`, and has `dispatch_interrupt(InterruptSource)` wired through to
it. `Teardown::interrupt_sources_released` is populated by
`destroy_thread()` calling `interrupts_.detach_all_owned_by(thread)` - a dying
driver's sources are already released and left masked, matching the
documented fail-closed rule.

**The ABI surface.** `core/oskernel/include/os/kernel/abi.hpp` already lists
`interrupt_attach = 11U`, `interrupt_detach = 12U`, `interrupt_complete = 13U`
in `KernelCall`. `begin_service` is deliberately not among them - M7.1's own
document explains why: it is the transition the kernel performs when it makes
the driver runnable, and the count rides back on the wakeup, so a fourth call
would grow the surface for information the driver is about to be handed
anyway.

## What is missing

Four gaps, each named already, in the place that named it:

1. **Capability-gated attach.** `docs/M6_0_DEVICE_ACCESS.md`: "No interrupt
   authority. Interrupt routing is deliberately absent." `docs/M7_1_INTERRUPT.md`,
   under "What is not decided yet": "Attaching the interrupt capability to
   the source... is the composition step where the ABI's `interrupt_control`
   authority becomes a capability check rather than a role." `InterruptTable`
   itself does not and should not consult `CapabilityTable` - its own header
   says so, for the same reason `IpcEndpointTable` and `Rendezvous` are kept
   apart: two state machines that each know about the other are two state
   machines neither of which can be tested alone. The check belongs one layer
   up, at the `Kernel` composition, where `ipc_send`/`ipc_receive`/`ipc_reply`
   already do exactly this shape of thing for IPC.

2. **The machine-layer first-level handler for device sources.**
   `docs/M7_1_INTERRUPT.md`: "Turning a real controller's signal into a call
   to `dispatch()` is machine-layer work and belongs with M7.3c." M7.5g built
   this for exactly one source - the kernel's own periodic timer, delivered
   straight to `PreemptionCoordinator::on_timer()`. `cookie_aarch64_irq_dispatch`
   in `aarch64_boot.cpp` has never routed any other GICv3 INTID anywhere.
   Device interrupts need the same acknowledge/mask/end-interrupt sequence the
   timer already uses, but ending in `Kernel::dispatch_interrupt()` and a
   scheduler wake instead of a preemption decision.

3. **Syscall-entry decoding.** `cookie_kernel_syscall_entry` decodes exactly
   one `KernelCall` today: `yield`. `interrupt_attach`, `interrupt_detach` and
   `interrupt_complete` are reserved in the ABI and unreachable from EL0.

4. **A real proof.** Every prior kernel milestone that claims something works
   proves it under QEMU with named serial markers `kernel-arm64-native`
   greps for, not just a host unit test. M7.9 needs the same: an EL0 driver
   process that attaches to a real interrupt source, is woken by a real
   assertion, services it, and completes it, observed end to end.

## Design: the capability composition

**Object identity.** Interrupt sources join the pattern IPC endpoints already
established: a tagged region of the 64-bit `ObjectId` space so a capability
minted for one kind of object can never be confused for another kind, even if
the numeric ordinal happens to collide.

```cpp
inline constexpr ObjectId interrupt_object_tag = 0x1EC0'0000'0000'0000ULL;
inline constexpr ObjectId interrupt_object_tag_mask = 0xFFFF'0000'0000'0000ULL;

[[nodiscard]] constexpr ObjectId interrupt_object_id(InterruptSource source) noexcept {
    if (source == invalid_interrupt_source) return invalid_object;
    return interrupt_object_tag | static_cast<ObjectId>(source);
}
```

Unlike `IpcEndpoint`, an `InterruptSource` carries no slot/generation pair to
pack in - it is already a flat, kernel-assigned, non-reused namespace
(`docs/M7_1_INTERRUPT.md`: "Source numbers are the kernel's own namespace and
the machine layer translates them"). The tag plus the raw source number is
the whole encoding.

**Rights.** One right, because there is one operation a capability over an
interrupt source authorizes: owning it.

```cpp
inline constexpr Rights interrupt_right_attach = 1U << 0U;
```

This is deliberately not modeled after `ipc_right_send`/`ipc_right_receive`'s
two-sided split. IPC has a sender and a receiver with genuinely different
authority; a source has exactly one owner doing exactly one kind of thing to
it. A second right would be a distinction with no operation behind it.

**Where the check lives.** `Kernel` gains three new methods, matching the
shape `ipc_send`/`ipc_receive`/`ipc_reply` already established: look up the
capability, verify it names this source and carries the right, then and only
then call into `InterruptTable`.

```cpp
[[nodiscard]] os::core::Result<void> interrupt_attach(
    ThreadId driver, CapabilityId source_capability) noexcept;
[[nodiscard]] os::core::Result<void> interrupt_detach(
    ThreadId driver, CapabilityId source_capability) noexcept;
[[nodiscard]] os::core::Result<bool> interrupt_complete(
    ThreadId driver, CapabilityId source_capability) noexcept;
```

`interrupt_complete` returns `Result<bool>`, not `Result<void>`, because
`InterruptTable::end_service` already returns whether the source must be
serviced again immediately - that answer does not stop mattering because a
capability check now sits in front of it.

The check itself: `capabilities().describe(source_capability)` to get
`CapabilityInfo`, confirm `holder == driver` (or the M7.8 execution-authority
form once a driver process is context-bound - see Open questions),
`(info.rights & interrupt_right_attach) != 0`, and
`info.object == interrupt_object_id(source)` where `source` is supplied by
the caller. A capability that names the wrong source, or lacks the right, or
belongs to someone else, fails before `InterruptTable` is ever consulted -
the same fail-closed posture M7.1's threat model already applies everywhere
else in this table.

**Why not IPC's pattern instead.** `IpcEndpointTable::send`/`receive` take
`CapabilityTable&` as a parameter and do the check internally. That was a
deliberate choice for IPC, not a default to inherit here.
`docs/M7_1_INTERRUPT.md` already commits to the opposite shape for
interrupts - "the table does not consult the capability table" - and that
commitment predates this document. Reopening it here to match IPC would be
changing a decision because a different subsystem happened to make a
different one, not because anything about interrupts changed.

## Design: machine-layer routing

`cookie_aarch64_irq_dispatch` currently has exactly one branch: acknowledge,
confirm the INTID matches the timer, call `on_timer()`, end the interrupt.
Device sources need a second branch, structurally parallel:

1. Acknowledge (`gic_v3_acknowledge()` - unchanged, already shared).
2. Translate the GIC INTID to a Cookie `InterruptSource`. This translation
   table is new: `interrupt.hpp` is explicit that source numbers are the
   kernel's own namespace so `interrupt_attach` does not change meaning per
   board, which means something has to own the INTID-to-source mapping. That
   something is the machine layer, per M7.1's own deferral, driven by the
   GICv3 topology M7.5g's discovery code already extracts from the device
   tree - the same discovery, a different consumer.
3. Call `Kernel::dispatch_interrupt(source)`. A `Dispatch::wake == true`
   result marks the owning driver thread runnable via the scheduler, exactly
   as `on_timer()` already marks the next process runnable - reusing the
   existing wake path rather than inventing a parallel one.
4. Mask at the controller if `InterruptTable` says the source is now masked
   (it always is, immediately after `dispatch()` - `docs/M7_1_INTERRUPT.md`:
   "A source is masked from the moment it is dispatched until its driver says
   the device is quiet"). Unmask happens on the machine layer's own timeline
   when `Kernel::interrupt_complete` succeeds and `InterruptTable` reports the
   source is attached again rather than pending.
5. End the interrupt at the controller (`gic_v3_end_interrupt()` - unchanged).

A spurious INTID - asserted but translating to no owned `InterruptSource`, or
translating to a source `InterruptTable` reports as unattached - is not a
`fail()` stage. `docs/M7_1_INTERRUPT.md` already answers this: "A line
asserting with no driver behind it is a hardware or configuration fault, not
a caller error," counted and reported, waking nobody. The machine layer
should end the interrupt and return, not halt.

## Design: the boot proof

M7.5i proved two EL0 processes and native IPC by installing two small
programs and watching a specific marker sequence. M7.9's proof follows the
same shape: one process minted a capability over a real interrupt source (the
timer's own PPI is the only source this board's `virt` topology reliably
offers pre-M7.9, so the first proof likely reuses it as a *device* source
rather than the scheduler's own timer path - see Open questions), attaching,
blocking on the wakeup, servicing, completing, and a marker for each
transition: `COOKIE:M7.9:ATTACHED`, `..:DISPATCHED`, `..:SERVICED`,
`..:COMPLETED`. `kernel-arm64-native` gates on all four appearing in that
order, the same discipline every prior kernel-arm64-native marker sequence in
this tree has kept.

## Threat model (additions to M7.1's)

M7.1's threat model covers a driver that already holds a source. This
document's addition is the step before that: who may come to hold one.

- *A thread with no capability over a source, but a valid capability over a
  different one.* `interrupt_attach` must fail with the same rejection a
  wrong-source IPC capability gets - `info.object` mismatch, checked before
  anything reaches `InterruptTable`. This is the interrupt-specific instance
  of the general rule M7.8 already established for IPC: a capability that
  names the wrong object is not almost-authority, it is no authority.
- *A thread that held a capability, was revoked, and races an in-flight
  attach.* `capabilities().describe()` is checked fresh on every call: no
  caching, no capability handle that outlives a revocation. The same
  discipline `ipc_send` already relies on.
- *A capability transferred to a second thread while the first still holds an
  attached source.* Two capabilities over the same object, two different
  holders, but `InterruptTable::attach` refuses a second owner
  (`source_taken`) regardless of which capability presented it. The
  capability layer can authorize *attempting* attach; only one attempt can
  ever succeed per source, which is `InterruptTable`'s own invariant and does
  not need restating here - it is why the layers are kept separate in the
  first place.

## Exit criteria

- `interrupt_attach`, `interrupt_detach` and `interrupt_complete` are
  capability-checked at the `Kernel` composition layer, with host-tested
  coverage of: correct source and right (succeeds), wrong object (fails),
  missing right (fails), revoked-then-attempted (fails), and a
  `detach`/re-`attach` cycle after the owner dies.
- `cookie_aarch64_irq_dispatch` routes at least one real, non-timer GICv3
  device source through `Kernel::dispatch_interrupt()`, with the INTID-to-
  `InterruptSource` translation driven by GICv3 topology discovery rather
  than a hardcoded constant.
- `cookie_kernel_syscall_entry` decodes and dispatches all three interrupt
  syscalls from EL0.
- `kernel-arm64-native` gates on an end-to-end marker sequence proving a real
  EL0 driver process attached to, was woken by, serviced and completed a
  device interrupt.
- `docs/M7_10_LINE_COUNT.md` and the gate script are updated in the same diff
  as whatever lines this adds, per the project's own rule.

## Open questions

**Which device source the first proof uses.** `virt`'s device roster under
QEMU without additional `-device` flags is thin - PL011 UART, the GICv3
itself, virtio-mmio slots, the architected timer. The UART is the most
realistic *driver* to prove first (Cookie's own boot code already depends on
it working), but wiring UART RX interrupts is more machinery than the proof
strictly needs to demonstrate the capability-gated attach path. Reusing the
already-discovered timer PPI as a stand-in *device* source for the first
proof - deliberately not through `PreemptionCoordinator`, straight through
`Kernel::dispatch_interrupt()` instead - proves the new path without also
building a UART driver. Owner's call; recorded rather than assumed, per this
project's own convention.

**Whether driver capabilities should be M7.8 execution-authority-bound from
the start.** M7.8 added `ExecutionAuthority`-bound capabilities specifically
so a recycled `ThreadId` cannot inherit standing authority. A driver process
is exactly the kind of long-lived, privileged-relative-to-its-device thread
M7.8's migration was written for. Starting `interrupt_attach` on the legacy
`ThreadId`-only path (as this document's signature above does) matches how
M7.6a's IPC syscalls launched before M7.8 existed, and M7.8.2's own deferred
"explicit `ExecutionAuthority` IPC path" suggests the project's own answer
for IPC is "add it once, after the ThreadId path is proven, not before." The
same order likely serves interrupts. Not decided here.

**Nested interrupts and priority.** `docs/M7_1_INTERRUPT.md` defers both
explicitly, on the grounds that they need numbers a real controller produces.
GICv3 topology discovery already runs; whether it discovers enough to answer
this remains open and is not part of this milestone's exit criteria.
