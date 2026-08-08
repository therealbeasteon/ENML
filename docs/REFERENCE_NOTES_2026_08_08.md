# Reference Notes — 2026-08-08

These notes capture architecture lessons from the additional source set supplied during M1.3-M1.5. They are design inputs, not copied implementation requirements, and they do not override ENML's frozen architecture or milestone gates.

## Samsung Knox Mobile Security White Paper, revision 1.1 (2026)

Useful direction for later security milestones:

- Preserve a hardware-rooted chain of trust across boot and runtime rather than treating verified boot as an isolated feature.
- Keep high-value keys outside normal-world application memory where target hardware permits it.
- Treat defense-in-depth as multiple independent barriers: boot integrity, runtime isolation, peripheral policy, application isolation, data protection, and attestation.
- Persistent rollback/tamper state can be security-relevant across later updates; an update should not automatically erase evidence that affects trust decisions.
- Device health/attestation is evidence for policy decisions; it should not become a universal bypass around normal application authorization.
- Software updates are a lifecycle with identification, integration, construction/QA, deployment testing, and release. Different update classes have different compatibility/stability risk.

M1.5 implication: uninstall/update state is durable system policy, not an application request field. ENML retains signer/generation identity across uninstall and does not model reinstall as a clean security-identity reset.

Do not copy Knox Warranty Bit, Knox Vault, Android APIs, Samsung fuse layouts, or vendor policy as ENML interfaces. Hardware-rooted rollback/attestation choices remain target-platform/security milestones.

## NIST FIPS 197-upd1 — Advanced Encryption Standard (2023 update)

- AES specifies AES-128, AES-192, and AES-256 with 128-bit blocks.
- AES is a block-cipher primitive; a real storage/protocol design still needs an appropriate approved/recommended mode and key-management construction.
- ENML must not invent a custom encryption mode merely because AES itself is standardized.

No storage crypto suite is frozen in M1.5. Crypto selection and hardware-backed key policy remain dedicated later security/storage milestones.

## BitLocker FIPS Security Policy

Useful architecture lessons, not a crypto-suite template:

- full-volume encryption is tied to early-boot integrity and a chain of trust;
- bulk-data keys and key-encryption/wrapping keys are separate concepts;
- raw high-value keys should not become general application-visible material;
- hibernation/swap/system-volume treatment matters to whole-device confidentiality;
- recovery and startup policy are security architecture, not UI-only features.

ENML should preserve the key-hierarchy/early-boot principles while choosing modern algorithms/modes independently. Historical BitLocker algorithm choices must not be copied as current requirements.

## Symbian OS Internals / Symbian Architecture Sourcebook / Smartphones and Symbian OS

Relevant principles retained by ENML:

- the process is a natural unit of trust because memory ownership/protection is already process-granular;
- capabilities/authorization and data caging are separate concerns and should remain separate mechanisms;
- client/server ownership of shared system resources is a recurring architecture pattern;
- loader/application-launch behavior belongs behind system policy rather than being delegated to arbitrary application paths;
- a Software Install Server is an explicit OS service, reinforcing that install/update/uninstall are managed system operations rather than arbitrary package scripts;
- load-on-demand and asynchronous service patterns can conserve constrained resources when used with bounded lifecycle policy;
- phone operating systems must remain frugal with memory, CPU, background activity, and power while surviving faulty third-party applications;
- middleware/framework APIs should abstract phone capabilities from applications;
- UI/platform policy can vary by form factor without changing the underlying service architecture.

M1.5 implication: one signer-bound application identity and user map to a durable application principal; every launched process still receives a fresh process identity and remains bound to its immutable package generation. Private application data is caged behind a system-provided root rather than a global path chosen by the app. Uninstall revokes launch/runtime authority without silently destroying the application identity or private-data profile.

## Operating-system structure references (Stallings; Silberschatz/Galvin/Gagne; Bach)

- program execution, process control, files, communication, error handling, resource allocation, and protection are OS services;
- applications should normally consume stable higher-level APIs rather than depend directly on kernel/system-call details;
- process isolation and message-passing boundaries remain appropriate foundations for ENML's service model;
- resource/protection policy belongs to the OS boundary, not individual untrusted programs;
- simple file/descriptor/process mechanisms are valuable implementation building blocks, but ENML's public application ABI should remain semantic and versioned.

Current implication: Linux `fork`, descriptors, `execveat`, Landlock, seccomp, PIDs, filesystem paths and private kernel APIs remain implementation details beneath App Manager, Storage Service and supervisor-owned identity.

## Linux kernel development / Bootlin kernel and driver material

Useful direction for hardware/BSP work:

- keep Linux kernel internals private; userspace ABI stability is not a promise that in-kernel APIs remain stable;
- upstream-first, reviewable patches reduce long-lived device maintenance cost;
- ARM64 cross-build, device tree, drivers, interrupts, DMA, MMIO and runtime PM are core porting concerns;
- driver/device power state is part of phone battery architecture, not a late optimization;
- keep target-specific BSP/driver work narrow and avoid third-party out-of-tree kernel-module sprawl where possible.

ENML continues to use Linux for the mature hardware ecosystem while presenting its own userspace service/API personality.

## C++ and Programming C# references

C++ direction retained by ENML:

- RAII/deterministic ownership for handles, locks and memory;
- strong types and small value abstractions around unsafe primitives;
- move semantics for ownership transfer;
- concurrency/synchronization only where needed and with explicit lifetime;
- low-level escape hatches isolated behind narrow reviewed interfaces.

C# material is used conceptually for interface/contracts, metadata/versioning, deployment boundaries, async I/O, threads/synchronization and isolated execution. It is not a requirement to introduce a CLR/JIT into the base OS.

## ARM assembly reference

The supplied assembly text is useful for load/store discipline, registers, stacks/calling conventions, addressing, machine-code reasoning and C/C++ interoperability.

It is primarily an ARM32 teaching reference. ENML remains AArch64-first. Assembly should stay limited to architecture-critical/measured code rather than becoming the default implementation language.

## The Little Book About OS Development / OS-from-scratch paper

These sources remain useful mainly for development sequencing and observability:

- build in small vertical slices and test each step;
- use emulators/VMs for speed but confirm hardware-sensitive behavior on real/native targets;
- reserve assembly for portions that genuinely require it;
- bring diagnostics, process/user separation, memory/filesystem mechanisms and testing online incrementally.

Their x86/BIOS/assembly-only example architecture is not an ENML design target. ENML continues to use Linux for the hardware/process kernel and validates separately on native AArch64.

## Mobile Network Security (2023)

Useful trust-boundary lessons:

- treat the cellular baseband/modem as a separate, potentially compromised computer rather than a trusted extension of the application processor;
- SIM/eSIM credential handling should remain separated from ordinary application memory/authority;
- fake base stations, downgrade behavior, signaling-system weaknesses and SIM-swap threats mean network identity is not equivalent to user/app cryptographic identity;
- SMS delivery is not a sufficient foundation for high-assurance authentication policy;
- modem DMA/device access should be tightly constrained by IOMMU/device policy where hardware supports it.

Future telephony continues through Telephony Service -> Modem Broker -> baseband with no raw application modem/APDU authority.

## Mobile performance testing and optimization (Ramu, 2023)

The source emphasizes mobile constraints and continuous measurement of CPU, memory, network behavior, battery use, background activity, latency, and responsiveness.

ENML follow-up:

- keep App Manager/storage launch paths bounded and avoid hidden worker pools/background loops;
- add launch-time, resident-memory, wakeup, CPU, network and background-work baselines as each subsystem stabilizes;
- exercise degraded/intermittent network conditions in later network-facing app/service tests;
- optimize background work, CPU/network activity and power behavior as continuously measured CI/product properties, not release-only cleanup.

## BlackBerry architecture and UI guidelines

Architecture lessons:

- public APIs are long-lived compatibility commitments and need formal quality/review discipline;
- APIs should be readable, domain-appropriate, extensible and difficult to misuse;
- simulator/debugger/performance tooling and signing/deployment belong to the developer platform, not as afterthoughts;
- security, networking and battery use are first-class phone-application concerns.

UI lessons:

- start with user goals/workflows, then screen organization and visual design;
- use standard, predictable components and forgiving behavior;
- localization/accessibility should be planned rather than retrofitted;
- evaluate real user workflows rather than judging isolated screens.

## Samsung One UI design guidance

Principles worth borrowing without copying Samsung visual identity:

- distinguish viewing/content zones from frequent interaction zones;
- keep common actions reachable, especially on large phones;
- adapt to phones, foldables/tablets and desktop-style modes through responsive layout rather than device-specific app forks;
- use safe margins/edge-aware layout;
- preserve semantic app bars, bottom actions/navigation, dialogs, lists, search and selection patterns;
- motion should explain spatial/interaction changes rather than decorate them;
- accessibility starts in planning, including large text, contrast, non-color-only signals and alternative interaction needs.

## Mobile app UI/UX and UX excerpt sources

Useful product rule: complexity should be hidden behind clear task-oriented flows rather than surfaced as implementation options. Future ENML settings/system UI should prefer semantic decisions and progressive disclosure over dense configuration pages.

## Bluetooth review/thesis source

The Bluetooth material is most useful for the future peripheral/connectivity boundary. Its treatment of authentication, authorization, confidentiality, discoverability, malformed-packet/fuzzing attacks and remote attack surface reinforces a hostile-input model for wireless stacks.

No Bluetooth architecture is added in M1.5. When that subsystem begins, it should sit behind a broker/service boundary, keep radio/protocol authority out of ordinary applications, and receive the same bounded-decoder/fuzzing treatment already used by OSIP and package ingestion.

## Panic passwords / duress-code references

These sources are useful as warnings against simplistic security UX:

- alternate-password/duress schemes can leak structure under repeated attempts or forced randomization;
- a deterministic PIN+1 rule is an example mechanism, not a secure default architecture;
- destructive wipe should not be the default hidden response to a duress credential.

If ENML later adds a duress/vault feature, it needs its own explicit threat model, data-consistency rules and coercion analysis rather than borrowing alarm-system behavior directly.

## NIST SP 800-124 Rev.1

The uploaded revision is withdrawn and superseded by Rev.2. It may be used only as historical mobile-security context. Do not freeze contemporary ENML requirements from the withdrawn revision when a current source is needed.

## Source-use rule

These references provide design evidence, threat models and implementation lessons. They are not a reason to copy obsolete protocols, historical crypto constructions, Android-specific APIs, Symbian ABI details, vendor-private security mechanisms, or an educational from-scratch kernel. ENML should preserve the principles that fit its architecture while independently freezing modern interfaces and security policy.
