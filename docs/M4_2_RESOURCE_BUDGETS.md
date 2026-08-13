# M4.2 Resource Budget Gate

## Why this exists

The project vision ranks "low resource consumption and small trusted components"
second only to security, and "low idle/background activity and power efficiency"
seventh. Until now those properties were architectural intentions enforced by
review: the invariants forbid hidden thread pools, unbounded queues, accidental
polling loops and independent animation timers, but nothing in CI could observe a
violation. The only measured resource property was the M0 zero-allocation gate on
the IPC hot path.

A property that cannot fail a build will drift. As the trusted surface grows
across the supervisor, App Manager, compositor, accessibility, storage and key
services, the per-service cost of "being an ENML service" needs the same kind of
falsifiable ceiling the allocation gate already provides.

## What is measured

`service_budget_test` supervises one real service, waits for READY, then samples
the child process through procfs across an idle window.

| Metric | Source | Meaning |
| --- | --- | --- |
| `resident_kib` | `VmRSS` | Resident set once the service is ready and idle. |
| `ready_ms` | monotonic clock around `Supervisor::start()` | This service's contribution to boot-to-shell. |
| `idle_wakeups_per_sec` | `voluntary_ctxt_switches` + `nonvoluntary_ctxt_switches` delta | Whether the service actually becomes quiet. |

The wakeup metric is the direct test of the "on-demand rather than always-on"
rule. A service correctly blocked in `poll()`/`epoll_wait()` with no work accrues
essentially no context switches. A nonzero floor means something is polling,
retrying on a timer, or ticking an animation independently of the compositor.

## Invariants

- Budgets are compile-time constants in `tests/budget/include/budget/budget.hpp`.
  There is no runtime config file, consistent with the existing rule against
  runtime YAML/JSON/XML in trusted components. Loosening a ceiling is a reviewed
  source diff, never an untracked environment change.
- Ceilings are set from measured behavior with deliberate headroom, not from
  aspiration.
- A service that improves should have its ceiling lowered in the change that
  earns the improvement. Otherwise the gate ratchets in one direction only and
  stops meaning anything.
- Budget checks never use `assert()`. Enforcement must be identical in every
  build configuration; a gate that evaporates under `NDEBUG` is not a gate.
- Restart policy is `never` during measurement. A service that dies mid-window
  must fail the gate, not be silently replaced by a fresh instance whose
  counters start at zero.
- Measurement support is probed first. A host that cannot report the required
  procfs fields skips (CTest exit 77) rather than passing vacuously.

## CI placement

The gate carries the `budget` label and runs in its own workflow. It is
deliberately **not** part of the frozen `m0` signal.

It is **not qemu-user-safe**: under emulation the observable resident set and
context-switch counts belong to the emulator process rather than to the service,
so the numbers are meaningless. The gate runs on native x86-64 and native
AArch64 runners only, matching the existing rule that native AArch64 is the
authoritative full-kernel behavior gate.

## Current coverage

`system.echo` only. It is the minimal ENML service — bootstrap, identity
publication and a bounded request loop, with no storage, keys or UI — so its
numbers are the floor cost of being an ENML service at all, and the earliest
warning that the substrate itself is getting heavier.

## Next

- Extend budgets to `system.storage`, `system.keys` and the compositor, whose
  costs are structural rather than baseline.
- Add a whole-system boot-to-shell budget once the M4 trusted shell can be
  brought up headlessly in CI.
- Consider a peak-RSS metric (`VmHWM`) alongside steady-state RSS to catch
  transient spikes during startup that steady-state sampling misses.
