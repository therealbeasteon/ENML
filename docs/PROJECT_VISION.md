# ENML OS — project vision

This document freezes the original product intent so future implementation does not drift as individual milestones become more detailed.

## Mission

Build a phone operating system with the compactness, modularity and appliance-like behavior associated with classic mobile operating systems such as Symbian, while applying modern security engineering and hardening as deeply as practical.

ENML is not a Symbian clone and does not copy Symbian ABI, UI, frameworks or historical implementation choices. Symbian and all other supplied sources are engineering evidence and design guidance.

## Implementation authority

**References teach principles. ENML determines implementation. External systems are not the design specification.**

This is a project-level rule, not merely a documentation preference. A supplied reference may teach a useful principle, mechanism/failure mode, threat-model lesson or engineering tradeoff. It does not define ENML's ABI, subsystem decomposition, wire protocol, visual grammar, service topology, naming, compatibility target or exact implementation.

For every design decision, begin from ENML's mission, threat model, resource/power constraints, existing invariants and measured behavior. Use the supplied references to challenge or inform that reasoning. Then implement the result in ENML's own types, ownership model and architecture.

A historical or external design is never accepted merely because it exists elsewhere. Likewise, ENML may choose a different implementation when that better satisfies ENML's security, bounded-resource, performance, portability or UX requirements while preserving the principle being learned.

## Priority order

The system must optimize the following together rather than treating any one as an afterthought:

1. security by default and strong hardening;
2. low resource consumption and small trusted components;
3. performance, fast startup/resume and immediate responsiveness;
4. modular subsystem boundaries with explicit ownership;
5. efficient, secure, developer-friendly APIs;
6. hardware portability and clean adaptation boundaries;
7. low idle/background activity and power efficiency;
8. excellent, accessible phone UX;
9. long-term maintainability and evolvability.

When priorities conflict, prefer an architecture that preserves the security boundary first, then reduce cost through better design rather than bypassing the boundary.

## Architectural character

### Small privileged core, strong services

Keep the privileged/trusted surface as small and reviewable as possible. Linux is currently the private kernel/hardware substrate, not ENML's public operating-system personality. ENML services and typed interfaces define the stable platform above it.

Use the Symbian microkernel/client-server lesson as a principle: keep essential responsibilities narrow and move shareable policy/resources behind explicit system-service boundaries where this improves isolation, restartability and modularity.

Do not mechanically reproduce Symbian's historical kernel, descriptors, cleanup stack, active-object ABI, window server, file server or DLL model.

### Process and capability oriented trust

Treat a process as a small unit of trust and bind authority to explicit capabilities/typed handles plus trusted system identity. Public request payloads must not be able to self-assert privileged identities, storage roots, cryptographic owners, secure display roles or similar authority.

Data caging, service-owned resources, exact owner checks, rights reduction and deterministic revocation remain core ENML patterns.

### On-demand rather than always-on

A mobile OS must not spend battery simply because capacity exists.

Prefer:

- event-driven work over polling;
- demand-loaded functionality over permanently resident optional components;
- bounded queues/tables over unbounded accumulation;
- compositor-driven animation ticks over independent animation timer loops;
- explicit one-shot retries over hidden reconnect workers;
- no unnecessary scanning, telemetry, background refresh or wakeups;
- no idle worker pools without a measured product requirement.

If a subsystem has nothing useful to do, it should be able to become quiet.

### Bounded resource use

Normal hot paths should have explicit limits for memory, IPC payloads, handles, objects and queued work. Avoid allocation patterns that make out-of-memory behavior unpredictable. Prefer caller-owned buffers, fixed-capacity state where appropriate and explicit backpressure/failure.

Performance counters/monitoring must themselves be designed so they do not become material background overhead.

### Hardware compatibility through isolation

Hardware-specific code belongs behind narrow adaptation/provider boundaries. Driver/kernel mechanisms should not leak into public application ABI.

The goal is to make a new SoC, display path, radio architecture, secure element or accelerator require changing the smallest practical implementation layer rather than rebuilding unrelated higher-level policy.

Prefer upstream Linux interfaces and small reviewable BSP changes. Do not fork broad kernel behavior merely for convenience.

## Security posture

### Secure by default

Default configuration must not require the user to discover hidden hardening switches before the phone becomes safe to use.

Core expectations include:

- least privilege;
- process isolation;
- attack-surface reduction;
- unnecessary service/driver removal;
- explicit application permissions/capabilities;
- private per-application data authority;
- non-exporting long-lived key services;
- strong authenticated encryption profiles rather than custom cryptography;
- secure update/rollback strategy as hardware integration matures;
- exploit mitigations and compiler/toolchain hardening where compatible with the performance budget;
- trusted-system UI that applications cannot counterfeit merely by copying pixels.

### Security is architectural, not cosmetic

Security-relevant state must have real technical authority underneath its presentation. A lock icon, color, dialog style or second password is never sufficient by itself.

Duress/panic authentication is therefore a future separately threat-modeled feature. Do not implement a naive second-PIN scheme and label it coercion resistant.

### Old references are not current compliance claims

Historical FIPS policies, withdrawn NIST revisions, old cellular mechanisms and vendor security architecture may provide useful design reasoning. They do not establish present-day compliance or justify obsolete cryptography/protocol choices.

## API philosophy

Public ENML APIs should be:

- semantic rather than Linux-mechanism-shaped;
- strongly typed;
- bounded;
- explicit about ownership and lifetime;
- deterministic in failure behavior;
- easy to use correctly;
- difficult to use to escalate authority;
- stable across hardware generations where practical;
- efficient enough for hot mobile paths without requiring applications to bypass system services.

Do not expose raw file paths, arbitrary daemon routing, DRM/KMS objects, provider key handles, native credentials or other implementation details merely to reduce engineering effort.

## Developer friendliness

Security and efficiency should not require hostile APIs.

The platform should provide:

- compact typed SDK surfaces;
- predictable lifecycle/state transitions;
- useful error domains;
- testable interfaces and host implementations;
- clear capability/permission declarations;
- reproducible build and CI paths;
- strong documentation of invariants;
- semantic UI components that include accessibility rather than requiring each app to rebuild it.

Complexity that is essential for security should be concentrated in platform implementations so ordinary app developers do not repeatedly reinvent dangerous mechanisms.

## UX and visual identity

ENML must have an original visual language rather than a reskin of Android, iOS, One UI, BlackBerry, Windows Phone or another platform.

The target character remains:

- classic and luxurious;
- crafted and artistic;
- dimensional rather than uniformly flat;
- colorful with deliberate hierarchy;
- authored curves/contours rather than repetitive generic rounded rectangles;
- capable of transparent/translucent/crystal/smoked/luminous materials;
- purposeful 3D, lighting, depth and animation;
- fast and legible even when premium effects are disabled;
- accessible under large text, high contrast, reduced motion and reduced transparency.

Transparency and animation are materials/tools, not the identity by themselves. Figure/ground, state, focus and secure-system attribution must remain clear without them.

## Performance and power gates

A feature is incomplete if its only validation is functional correctness.

As subsystems mature, track appropriate budgets for:

- cold boot/startup latency;
- resume latency;
- UI input-to-present latency;
- frame deadlines/jank;
- idle wakeups;
- background CPU time;
- resident/private memory;
- IPC count/bytes on hot flows;
- network/radio wakeups;
- storage I/O amplification;
- battery/power impact.

Do not add permanent monitoring overhead merely to collect these metrics. Prefer test/profiling builds and low-cost platform counters.

## Reference policy

All supplied books, papers, standards, architecture guides, UI guides and security documents are to be used strictly as guides.

For every reference-driven change:

1. identify the principle or failure mode being borrowed;
2. decide whether it is still valid for ENML's threat model and hardware era;
3. derive the implementation from ENML's own requirements and existing architecture;
4. express the result using ENML's own types and ownership model;
5. avoid copying vendor-specific ABI, subsystem topology, visual identity or obsolete algorithms;
6. validate the resulting behavior with ENML-specific tests and measured behavior.

A reference is evidence in the reasoning process, never an implementation specification. When several references demonstrate different mechanisms for the same principle, ENML is free to use none of those mechanisms and design its own, provided the resulting design satisfies ENML's requirements and is validated.

See `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md`, `docs/REFERENCE_ADDITIONS_2026_08_10.md` and milestone-specific reference notes.

## Current M3.2 interpretation

The current semantic UI/display work should continue to honor the product mission by:

- keeping application UI semantic and bounded;
- keeping rendering/compositor hardware details private;
- using caller-owned/bounded pixel paths where possible;
- moving only when a compositor tick requires it rather than running animation threads;
- degrading materials and motion for power/accessibility without losing ENML identity;
- keeping secure-system presentation compositor-authoritative;
- maintaining strong text/font/provider boundaries;
- validating GCC, Clang, sanitizers and native ARM64 before declaring a slice complete.

Later milestones must carry the same priorities into input, shell, telephony/radio, hardware display, verified boot, secure-element providers, update/recovery and power management.
