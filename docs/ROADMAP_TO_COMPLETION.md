# Cookie OS — Roadmap to Completion

Status legend: `DONE` means its gate has been demonstrated; `ACTIVE` is the only milestone being advanced; `NEXT` is unblocked by the active milestone; `LOCKED` depends on earlier gates.

## Operating rule

A milestone does not advance because code exists. It advances only when its required host tests, freestanding AArch64 build checks, boot/runtime probes where applicable, and security invariants have actually executed successfully. Each milestone stays on its own branch/PR until its gate is satisfied.

## Current position — M7.8 Context-Bound Authority (`ACTIVE`)

The standalone kernel already contains capability transfer/revocation, synchronous IPC, scheduling, interrupt/timer discovery, AArch64 exception/EL0/context-switch machinery, page-table construction, ASID lifecycle quarantine, process translation bindings, split user/kernel translation work, and a freestanding AArch64 image target.

M7.8 exists to close an identity boundary before more machine state is trusted: a recyclable hardware ASID must never become a software security principal. `AddressSpaceIdentity` is therefore `(slot, generation)` and `ExecutionAuthority` is `(ThreadId, AddressSpaceIdentity)`.

### M7.8 gate

- Capability ownership and use can be bound to `ExecutionAuthority`, not only `ThreadId`.
- IPC authorization accepts a resolved live execution authority.
- Syscall entry resolves authority from the active process translation binding; user registers cannot nominate their own identity.
- Same ThreadId + recycled same ASID + new address-space generation cannot exercise stale capability/IPC/reply/continuation authority.
- Thread/address-space teardown invalidates context-bound authority deterministically.
- Host adversarial tests execute successfully.
- Freestanding AArch64 kernel target builds successfully and the runtime probe/boot gate executes successfully before merge.

## Kernel-completion sequence

### M7.8.1 — Context-bound capability holders (`ACTIVE`)
Introduce a shared execution-authority type and context-bound capability operations. Preserve the pure bounded capability state machine and derivation-tree revocation. Add stale-generation tests.

Exit gate: a capability minted/granted to one execution authority cannot be held, granted, or revoked by the same ThreadId in another address-space generation.

### M7.8.2 — Context-bound IPC (`NEXT`)
Make endpoint authorization, pending calls, completed replies, and reply seals consume/record context-bound authority where identity matters.

Exit gate: stale generations cannot send, receive, collect a reply, or reuse a transaction/reply relationship after ThreadId/ASID recycling.

### M7.8.3 — Trusted syscall identity (`LOCKED`)
Resolve the caller from the live `ProcessTranslationTable` at the exception/syscall boundary. The syscall ABI supplies operation arguments, never caller identity.

Exit gate: forged caller identifiers and stale translation epochs fail closed on host and AArch64 paths.

### M7.8.4 — Authority teardown and recycle torture (`LOCKED`)
Unify thread, continuation, endpoint, capability, interrupt, scheduler, translation-root, and epoch retirement ordering. Add repeated recycle/adversarial tests.

Exit gate: no authority survives teardown/reuse and no ASID is reused before architectural retirement completes.

### M7.9 — User virtual-memory lifecycle (`LOCKED`)
Complete user address-space ownership: reviewed mapping API, page ownership/accounting, stack/guard regions, W^X, executable-image mapping, mapping retirement, fault classification, and copy-to/from-user integration.

Exit gate: an EL0 process can be created, mapped, faulted, repaired/terminated according to policy, and destroyed without leaking mappings or authority.

### M7.10 — SMP, preemption, interrupt and TLB correctness (`LOCKED`)
Move from single-core-safe assumptions to explicit per-CPU state, timer preemption, interrupt routing, cross-core scheduling rules, ASID/TLB shootdown protocol, and rendezvous needed for translation retirement.

Exit gate: multi-vCPU QEMU stress tests execute with forced preemption and repeated map/unmap/process churn without stale translations or scheduler corruption.

### M7.11 — Standalone kernel release gate (`LOCKED`)
Close the kernel as a product boundary: deterministic boot path, panic/crash record, boot memory ownership, kernel mapping finalization, no unintended Linux/runtime dependency, reproducible freestanding image, ABI/version contract, and QEMU test harness.

Exit gate: Cookie kernel boots from its own image, reaches EL0, services IPC/timer/fault paths, survives stress, and passes the security/adversarial suite without a Linux kernel underneath it.

## Native OS sequence after the kernel gate

### P8 — Native process/service bootstrap (`LOCKED`)
Replace host/Linux-backed execution assumptions in the production path with Cookie-native process loading, service startup, IPC transport, supervision, and recovery.

Exit gate: the minimal service graph boots entirely as Cookie EL0 processes over Cookie kernel primitives.

### P9 — Persistent storage, key hierarchy, packages and updates (`LOCKED`)
Port the existing storage/key/package policies to native kernel services and drivers; integrate encrypted-at-rest storage, secure key lifecycle, verified packages, A/B or equivalent safe update flow, rollback protection, recovery, and attestation hooks.

Exit gate: install/update/reboot/rollback/recovery and credential/key destruction scenarios execute without host OS security dependencies.

### P10 — Device and driver substrate (`LOCKED`)
Bring up the target hardware contract: serial/debug, timers/GIC, block storage, display, touch/input, USB, power/thermal, secure element/TEE interface where available, radios through isolated driver/service domains, and IOMMU/DMA policy where hardware supports it.

Exit gate: target board reaches the native shell with storage, display/input, power and required communications devices functioning through isolated services.

### P11 — Native display, input, compositor and shell (`LOCKED`)
Move the existing UI/compositor/input architecture onto native services and drivers. Preserve trusted overlays/consent surfaces, accessibility, responsive layout and low-memory behavior.

Exit gate: lock screen, launcher/task model, settings/consent, app surface composition and trusted security UI operate natively on target hardware.

### P12 — Application platform and compatibility boundary (`LOCKED`)
Finalize package identity, app sandbox, permissions/capability brokerage, lifecycle, background limits, notifications, media/network/storage APIs, SDK/IDL tooling and compatibility policy.

Exit gate: third-party test apps install, launch, communicate only through granted interfaces, survive lifecycle events, and cannot cross sandbox/capability boundaries.

### P13 — Phone/security product hardening (`LOCKED`)
Complete verified boot chain integration, measured/attested state, disk/user-data encryption, duress credential policy, rate limiting, secure recovery, anti-rollback, exploit mitigations, logging/audit boundaries, privacy controls, radio/baseband isolation assumptions, and threat-model review.

Exit gate: documented attack scenarios have automated or reproducible verification and critical security invariants fail closed.

### P14 — Performance, power, reliability and accessibility (`LOCKED`)
Profile boot, IPC, scheduler, compositor, storage and crypto paths; implement suspend/resume, idle states, memory pressure, watchdog/restart policy, crash-loop handling, thermal/power budgets, accessibility completeness and long-duration stress testing.

Exit gate: target performance/power budgets and soak/recovery tests are met on real hardware.

### P15 — Release engineering and completion (`LOCKED`)
Reproducible builds, signed artifacts, update/recovery images, manufacturing/provisioning flow, release-key procedure, SBOM/dependency audit, source/reference traceability, developer docs, threat model, test evidence, user recovery docs and release checklist.

## Definition of Cookie OS complete

Cookie OS is complete for its first production-capable release when a reproducible signed image boots on the chosen ARM64 target without a Linux kernel underneath; initializes the native Cookie kernel and service graph; provides persistent encrypted storage, key/package/update/recovery flows; reaches the native lock screen/shell; runs sandboxed applications through capability-brokered APIs; supports the required device hardware; passes adversarial security, fault/recovery, multi-core, performance, power and long-duration reliability gates; and has a documented repeatable build, flash, update, recovery and release process.

## Progress rule

Development follows this file in order. New work may be researched in parallel, but implementation does not bypass a locked dependency. When a gate is demonstrated, update its status here in the same PR (or the immediate successor PR) and advance exactly one `ACTIVE` milestone.