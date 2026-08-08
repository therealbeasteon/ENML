# Reference Notes — 2026-08-08

These notes capture architecture lessons from the additional source set supplied during M1.3. They are design inputs, not copied implementation requirements, and they do not override ENML's frozen architecture or current milestone gates.

## Samsung Knox Mobile Security White Paper, revision 1.1 (2026)

Useful direction for later M2/M3 hardening:

- Preserve a hardware-rooted chain of trust across boot and runtime rather than treating verified boot as an isolated feature.
- Keep high-value keys outside normal-world application memory where target hardware permits it.
- Treat defense-in-depth as multiple independent barriers: boot integrity, runtime isolation, peripheral policy, application isolation, data protection, and attestation.
- Device health/attestation is evidence for policy decisions; it should not become a universal bypass around normal application authorization.

M1.3 implication: App Manager must consume already-trusted package identity/generation state and may not let the application choose its identity, executable, or sandbox.

## NIST FIPS 197-upd1 — Advanced Encryption Standard (2023 update)

- AES specifies AES-128, AES-192, and AES-256 with 128-bit blocks.
- AES is a block-cipher primitive; a real storage/protocol design still needs an appropriate approved/recommended mode and key-management construction.
- ENML must not invent a custom encryption mode merely because AES itself is standardized.

No crypto suite is frozen in M1.3. Crypto selection remains a dedicated security milestone.

## Symbian OS Internals / Symbian architecture material

Relevant principles retained by ENML:

- The process is a natural unit of trust because memory ownership/protection is already process-granular.
- Capabilities/authorization and data caging are separate concerns and should remain separate mechanisms.
- Loader/application-launch behavior belongs behind system policy rather than being delegated to arbitrary application paths.
- Phone operating systems must remain frugal with memory, CPU, background activity, and power while surviving faulty third-party applications.

M1.3 implication: one installed signer-bound application identity maps to one trusted application principal across immutable generations; every launched instance receives a fresh process identity and remains bound to the generation from which it was launched.

## Operating-system structure references (Stallings; Silberschatz/Galvin/Gagne)

- Program execution, process control, files, communication, error handling, resource allocation, and protection are OS services.
- Applications should normally consume stable higher-level APIs rather than depend directly on kernel/system-call details.
- Process isolation and message-passing boundaries remain appropriate foundations for ENML's service model.

M1.3 implication: Linux `fork`, descriptor operations, `execveat`, and PIDs remain private implementation details beneath App Manager and supervisor-owned identity.

## Mobile performance testing and optimization (Ramu, 2023)

The source emphasizes mobile constraints and continuous measurement of CPU, memory, network behavior, battery use, background activity, latency, and responsiveness.

ENML follow-up:

- Keep App Manager launch bounded and synchronous only for this milestone; avoid hidden worker pools/background loops.
- Add launch-time, memory, wakeup, and background-work baselines once the app lifecycle is stable.
- Continue running performance/resource checks in CI instead of treating optimization as a final release-only phase.

## Bluetooth review/thesis source

The Bluetooth material is most useful for the future peripheral/connectivity boundary: authentication, authorization, confidentiality, malformed packet/fuzzing threats, discoverability, and remote attack surface all support treating wireless protocol stacks and peer-provided bytes as hostile inputs.

No Bluetooth architecture is added in M1.3. When that subsystem begins, it should sit behind a broker/service boundary and receive the same bounded-decoder/fuzzing treatment already used by OSIP and package ingestion.
