# P8 Native Service Bootstrap — de-Linux Execution Inventory

Status: PREPARATION ONLY. P8 implementation remains locked until M7.11 passes.

## Objective

Move the production Cookie service graph from Linux-hosted transport/process primitives onto Cookie Kernel without recreating UNIX as an internal compatibility layer. Host Linux remains permitted for development tools, tests, CI and transitional adapters only; it is not part of the production Cookie OS trust boundary.

The repository's M7.2 coupling gate is the baseline inventory. It freezes the current production Linux-coupled surface and prevents accidental growth while Cookie-native replacements are built.

## Architecture rule

Cookie-native replacements must expose Cookie concepts, not Linux-shaped abstractions:

- authenticated `ExecutionAuthority` / kernel sender identity instead of PID/UID credential trust;
- Cookie capability transfer instead of fd passing / `SCM_RIGHTS`;
- Cookie rendezvous/endpoints instead of `AF_UNIX`/`socketpair` as the product IPC ABI;
- Cookie process/thread lifecycle instead of `fork`/`exec`/`pidfd` ownership semantics;
- Cookie memory-object/buffer authority instead of Linux fd-backed shared-memory contracts;
- kernel-enforced least authority instead of userspace sandbox claw-backs based on `prctl`, seccomp, UID/GID changes or namespace setup.

Linux compatibility adapters may translate at a development boundary, but no production service protocol may require Linux identifiers, descriptors, pathname sockets, credentials or process semantics.

## Migration order

### P8.1 — Native IPC substrate

Entry gate: M7.11 standalone kernel.

Deliverables:

1. Kernel IPC reaches stable generation-bound `ExecutionAuthority` semantics.
2. Service-side endpoint ownership and client authority are kernel-derived.
3. Capability transfer is explicit, typed and attenuating.
4. Endpoint retirement deterministically wakes/cancels callers without user cleanup.
5. A native service registry/bootstrap protocol uses Cookie endpoints and capabilities only.

Exit gate:

- production service-to-service communication requires no `AF_UNIX`, `socketpair`, `SCM_RIGHTS`, Linux credential query or Linux fd identity;
- sender identity cannot be supplied by userspace;
- crash/restart tests prove stale endpoint and stale authority rejection.

### P8.2 — Native process supervision

Deliverables:

1. Kernel process creation and destruction have explicit ownership/lifecycle authority.
2. Service supervisor starts a fixed signed bootstrap graph without `fork`/`exec` semantics leaking into the product ABI.
3. Service death revokes owned capabilities, retires endpoints, drops mappings and cancels in-flight IPC.
4. Restart creates a fresh execution/address-space generation; previous authority cannot be replayed.

Exit gate:

- production supervision requires no PID-as-principal, `fork`, `exec`, `waitpid`, `pidfd`, Linux signal ownership or `/proc` contract;
- mandatory services can be killed/restarted under fault injection without stale capability or IPC inheritance.

### P8.3 — Native shared memory and display buffers

Deliverables:

1. Cookie memory objects have kernel-owned identity, size, mapping rights and lifecycle.
2. Mapping authority is separate from ownership and may be attenuated/revoked.
3. Display/compositor buffers are transferred by Cookie capability, not Linux descriptor.
4. DMA-capable buffers eventually compose with P10 IOMMU/device-domain authority rather than creating a second memory-ownership model.

Exit gate:

- no production graphics/data path assumes a Linux fd is a buffer identity;
- stale handles after process restart or buffer retirement fail closed;
- mapping permissions cannot exceed the originating memory-object authority.

### P8.4 — Native service graph bootstrap

Minimal trusted bootstrap order:

1. Cookie Kernel establishes verified machine state and initial process authority.
2. A minimal bootstrap/supervisor process receives only the capabilities needed to start the base graph.
3. Key/storage/update policy services start before consumers of persistent sensitive state.
4. Input/display/shell and application-facing brokers start only after their dependencies are live.
5. Optional/network-facing services are later leaves, not prerequisites for unlocking local private data.

The graph must be explicit and cycle-checked. A service receives only declared capabilities at creation; there is no ambient global service namespace granting authority by name alone.

Exit gate:

- a cold boot reaches the native base service graph without a Linux process substrate;
- dependency failure produces a deterministic degraded/recovery state rather than broad privilege fallback;
- no service gains authority merely because it starts early.

### P8.5 — Delete Linux sandbox claw-backs from production

Only after native authority/process/memory boundaries exist:

- remove production reliance on `prctl`, seccomp, UID/GID dropping and Linux namespace setup;
- keep host sandboxing only around host tools/tests where useful;
- tighten the Linux-coupling ceiling downward with every deletion until production coupling reaches zero.

## Workstream inventory

| Workstream | Transitional Linux concept | Cookie-native target | Depends on |
|---|---|---|---|
| IPC transport | Unix sockets/socketpair | kernel endpoints + rendezvous | M7.8–M7.11 |
| Sender identity | PID/UID/peer credentials | `ExecutionAuthority` | M7.8 |
| Handle transfer | fd/SCM_RIGHTS | capability transfer/attenuation | M7.8, P8.1 |
| Process lifecycle | fork/exec/wait/pidfd | kernel process objects + supervisor authority | M7.9–M7.11, P8.2 |
| Shared memory | fd-backed mapping | Cookie memory object | M7.9, P8.3 |
| Display buffers | Linux/native handles | memory/buffer capabilities | P8.3, P10, P11 |
| Sandbox | UID/GID/seccomp/prctl | capability-first creation + kernel isolation | M7.11, P8.2 |
| Service discovery | pathname/global namespace | capability-mediated registry/bootstrap | P8.1, P8.4 |

## Security invariants

- No production public ABI uses PID, UID, GID, Linux fd or socket pathname as authority.
- Numeric object IDs are lookup keys, never principals.
- Reuse of a thread/process/address-space slot cannot revive prior IPC, mapping or service authority.
- Service restart narrows or recreates authority; it never silently inherits the previous process incarnation.
- Capability delegation is monotonic: rights and context may stay equal or narrow, never broaden.
- Administrative teardown is kernel-owned and may clean all state for a dead object even when user authority has expired.
- Host adapters cannot become a required runtime security boundary.

## Validation matrix

P8 is not complete until CI includes:

- native service ping/reply using Cookie IPC only;
- receiver-first and sender-first IPC under restart/fault injection;
- stale process-generation replay rejection;
- endpoint retirement with blocked callers;
- capability-transfer attenuation tests;
- supervisor crash/restart tests;
- memory-object transfer/map/revoke tests;
- a production-image coupling scan proving forbidden Linux primitives are absent from the native service graph;
- QEMU ARM64 boot of the native graph before hardware-target promotion.

## Non-goals

P8 does not invent new cryptographic primitives, emulate POSIX for convenience, make Linux identifiers permanent Cookie ABI, or move device drivers into the service graph before the P10 driver/device authority model is ready.
