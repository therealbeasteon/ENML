# Reference Notes — 2026-08-08

These notes capture architecture lessons from the additional source set supplied during M1.3/M1.4. They are design inputs, not copied implementation requirements, and they do not override ENML's frozen architecture or milestone gates.

## Samsung Knox Mobile Security White Paper, revision 1.1 (2026)

Useful direction for later security milestones:

- Preserve a hardware-rooted chain of trust across boot and runtime rather than treating verified boot as an isolated feature.
- Keep high-value keys outside normal-world application memory where target hardware permits it.
- Treat defense-in-depth as multiple independent barriers: boot integrity, runtime isolation, peripheral policy, application isolation, data protection, and attestation.
- Persistent rollback/tamper state can be security-relevant across later updates; an update should not automatically erase evidence that affects trust decisions.
- Device health/attestation is evidence for policy decisions; it should not become a universal bypass around normal application authorization.

Current implication: App Manager consumes already-trusted package identity/generation/profile state and never lets an application choose its identity, executable, data root, or sandbox. M1.5 update/uninstall work should similarly keep rollback/update state separate from application requests.

## NIST FIPS 197-upd1 — Advanced Encryption Standard (2023 update)

- AES specifies AES-128, AES-192, and AES-256 with 128-bit blocks.
- AES is a block-cipher primitive; a real storage/protocol design still needs an appropriate approved/recommended mode and key-management construction.
- ENML must not invent a custom encryption mode merely because AES itself is standardized.

No storage crypto suite is frozen in M1.4. Crypto selection and hardware-backed key policy remain dedicated later security/storage milestones.

## Symbian OS Internals / Symbian architecture material

Relevant principles retained by ENML:

- The process is a natural unit of trust because memory ownership/protection is already process-granular.
- Capabilities/authorization and data caging are separate concerns and should remain separate mechanisms.
- Client/server ownership of shared system resources is a recurring architecture pattern.
- Loader/application-launch behavior belongs behind system policy rather than being delegated to arbitrary application paths.
- Load-on-demand and asynchronous service patterns can conserve constrained resources when used with bounded lifecycle policy.
- Phone operating systems must remain frugal with memory, CPU, background activity, and power while surviving faulty third-party applications.
- Symbian's software-install server is an explicit system service, reinforcing that installation is a managed OS operation rather than arbitrary application script execution.

Current implication: one signer-bound application identity and user map to a durable application principal; every launched process still receives a fresh process identity and stays bound to its immutable package generation. Private application data is caged behind a system-provided root rather than a global path chosen by the app.

## Smartphones and Symbian OS

The supplied smartphone chapter is especially useful as a product-level constraint source. It describes mobile hardware as resource-limited relative to desktop systems and explicitly calls for frugal memory use, robustness in the presence of faulty third-party software, flexible UI across phone form factors, and middleware/framework APIs that abstract phone capabilities.

ENML implication: security boundaries must not be bought with desktop-style daemon sprawl or persistent background activity. New services should stay narrow, lazy/on-demand where appropriate, measurable, and survivable under app failure.

## Operating-system structure references (Stallings; Silberschatz/Galvin/Gagne)

- Program execution, process control, files, communication, error handling, resource allocation, and protection are OS services.
- Applications should normally consume stable higher-level APIs rather than depend directly on kernel/system-call details.
- Process isolation and message-passing boundaries remain appropriate foundations for ENML's service model.
- Resource and protection policy belongs to the OS boundary, not to individual untrusted programs.

Current implication: Linux `fork`, descriptors, `execveat`, Landlock, seccomp, PIDs and filesystem paths remain private implementation details beneath App Manager, Storage Service and supervisor-owned identity.

## The Little Book About OS Development / OS-from-scratch paper

These sources remain useful mainly for development sequencing and observability:

- build in small vertical slices and test each step;
- use emulators/VMs for speed but confirm hardware-sensitive behavior on real/native targets;
- reserve assembly for portions that genuinely require it;
- bring diagnostics, process/user separation, memory/filesystem mechanisms and testing online incrementally.

Their x86/BIOS/assembly-only example architecture is not an ENML design target. ENML continues to use Linux for the hardware/process kernel and validates separately on native AArch64.

## Mobile performance testing and optimization (Ramu, 2023)

The source emphasizes mobile constraints and continuous measurement of CPU, memory, network behavior, battery use, background activity, latency, and responsiveness.

ENML follow-up:

- Keep App Manager launch bounded and avoid hidden worker pools/background loops.
- Add launch-time, resident-memory, wakeup, CPU, network and background-work baselines once the application lifecycle/update path is stable.
- Exercise degraded/intermittent network conditions in later network-facing app/service tests.
- Continue performance/resource checks in CI instead of treating optimization as a final release-only phase.

## Bluetooth review/thesis source

The Bluetooth material is most useful for the future peripheral/connectivity boundary. Its treatment of authentication, authorization, confidentiality, discoverability, malformed-packet/fuzzing attacks and remote attack surface reinforces a hostile-input model for wireless stacks.

No Bluetooth architecture is added in M1.4. When that subsystem begins, it should sit behind a broker/service boundary, keep radio/protocol authority out of ordinary applications, and receive the same bounded-decoder/fuzzing treatment already used by OSIP and package ingestion.

## Source-use rule

These references provide design evidence, threat models and implementation lessons. They are not a reason to copy obsolete protocols, historical crypto constructions, Android-specific APIs, Symbian ABI details, or an educational from-scratch kernel. ENML should preserve the principles that fit its architecture while independently freezing modern interfaces and security policy.
