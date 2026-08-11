# Reference additions — 2026-08-11

> **References teach principles. ENML determines implementation. External systems are not the design specification.**

This note records the reference material supplied by the project owner on 2026-08-11 and how it may influence ENML. It does **not** import another platform's ABI, service topology, visual grammar, protocol choices, cryptographic construction, or compatibility model.

The allowed use is narrower: extract a security principle, threat, failure mode, engineering trade-off, or evaluation technique; state the ENML threat model; then derive an ENML-owned mechanism that satisfies the project's security, resource, power, responsiveness, and usability constraints.

## Supplied material

1. `01-11-2024-1730459061-6-IJGET-9.AdvancedBootloaderDesignforEmbeddedSystems_secureandefficientfirmwareupdates.pdf` — secure bootloader / firmware-update principles.
2. `2.-Compter-Science-Syeda-Tooba-Kazmi.pdf` — Android compromise examples and user-facing attack awareness.
3. `5.-Mobile-app-UIUX-Design(1).pdf` — basic mobile UX/UI workflow material.
4. `06-sandboxing.pdf` — confinement, reference-monitor requirements, jail limitations, and system-call mediation.
5. `9ae82cb18283b325c7681d5d0e795de66b9c.pdf` — operating-system structures and subsystem responsibilities.
6. `12-344-154355665249-52.pdf` — Android backdoor threat examples.
7. `12-610-157770877159-63(1).pdf` — educational operating-system-from-scratch material.
8. `49c52b6cdc25db32b5db0bc9c18e8e7c913f.pdf` — RAT/social-engineering data-theft threat examples.
9. `140sp1053(1).pdf` — BitLocker FIPS 140-2 security policy; historical cryptographic boundary/key-lifecycle material.
10. `140sp3725.pdf` — OpenSSL FIPS 140-2 non-proprietary security policy; cryptographic module boundary, roles, key management, zeroization, and self-test material.
11. `225(2).pdf` — Android mobile OS/architecture overview; layering/isolation observations only, not an ENML architecture template.
12. `373b72ec7b04dab558a5415cbb106901.pdf` — Merkle-tree integrity/authentication concepts.
13. `978-3-642-39218-4_16_Chapter.pdf` — mobile-device encryption systems and deployment/configuration failure modes.
14. `2008-142.pdf` — reduced-round SHA-256 cryptanalysis; useful principally as a reminder not to infer full-primitive weakness from reduced variants and not to invent reduced/custom cryptography.
15. `2021-1054.pdf` — one-time traceable ring signatures; specialized privacy/authentication research.
16. `2022-545.pdf` — logic locking and hardware supply-chain threat/modeling material.
17. `2022-1730.pdf` — Merkle Tree Ladder mode and post-quantum signature-size trade-offs.
18. `2022-1743.pdf` — ring signatures with user-controlled linkability; specialized privacy/authentication research.
19. `2023-03-24-KeyStorage.pdf` — mobile key management, secure storage, non-exportable integrated hardware, and key-lifecycle material.
20. `2023-06-02-Mobile-Network-Security(2).pdf` — cellular authentication/confidentiality/integrity, downgrade, unauthenticated signaling, location/privacy, and legacy-interoperability threats.

## Principles accepted into ENML review

### Boot and update security

A firmware update is not trusted because it arrived through an update channel. Authenticity and integrity must be established before execution, and boot must begin from a trusted verification decision rather than from the assumption that persistent storage is honest.

The bootloader reference discusses rollback as a reliability mechanism after a failed update. ENML separates that from **security anti-rollback**. A/B or recovery fallback may return to a known-good image only when that image remains permitted by the monotonic security-version policy. Reliability rollback must never become a path to deliberately boot a previously vulnerable release.

Updates remain transactional, bounded, power-failure-aware, and authenticated. Delta transport may reduce bytes, but the reconstructed artifact is verified as a complete trusted object before it becomes bootable.

### Confinement and the reference monitor

The sandboxing material's strongest reusable principle is the reference-monitor requirement: security-sensitive application requests must be mediated, the mediator must not be bypassable by the application, and the enforcing code should remain small enough to analyze.

ENML therefore does not treat a filesystem chroot, UID separation, or a package permission declaration as sufficient confinement. Applications receive semantic, revocable capabilities to services and objects; they do not receive ambient raw device, storage, compositor, input, radio, or key authority. A process that acquires an unexpected native descriptor or guesses a numeric service identifier has still not acquired ENML authority.

The source's jail-escape examples are treated as failure modes: privileged escape hatches, raw-device access, unrestricted network access, signal/control authority over unrelated processes, and policy that is only path-based are not acceptable substitutes for capability-scoped mediation.

### Keys, encrypted data, and forensic exposure

The supplied key-storage and mobile-encryption material reinforces separation among bulk-data encryption, key management, key wrapping/derivation, and protected key storage. ENML's production direction remains:

- data keys are not application-readable root secrets;
- higher-value roots are non-exportable when the hardware permits it;
- key scope follows system -> user/profile -> application authority rather than caller-selected paths;
- revocation/destruction and zeroization are explicit lifecycle operations;
- encrypted data at rest is not considered protected merely because an encryption algorithm appears somewhere in the stack;
- recovery/update policy must not quietly introduce a second key-extraction path.

Historical FIPS policy documents are useful for thinking about explicit cryptographic boundaries, roles, key entry/output, storage, zeroization, startup/integrity self-tests, and failure states. They are **not** a claim that ENML is FIPS validated, and obsolete suites/configurations are not inherited.

Forensics resistance is treated as an end-to-end property of boot state, key availability, memory lifetime, recovery behavior, logging/artifact policy, and hardware. ENML will not claim that deleting a filename, encrypting one partition, or implementing a duress action makes a device forensically indistinguishable.

### Backdoors, RATs, and malicious applications

The Android backdoor/RAT papers are threat examples, not architecture references. They illustrate the consequence of allowing a malicious application to turn installation or social engineering into ambient access to messages, contacts, location, microphone, camera, storage, or network control.

ENML's derived rules are stricter:

- installation does not imply sensor/data/network authority;
- high-impact resources are separately capability-scoped and revocable;
- privileged system services authenticate the live caller rather than trusting caller-supplied identity;
- hidden debug/factory endpoints may not bypass production authorization;
- executable provenance and update policy are part of the trust chain;
- system UI must not train the user to approve opaque privilege escalation.

No intentional production backdoor, master credential, universal service token, undocumented privileged socket, or factory bypass is an acceptable ENML feature.

### Mobile-network boundary

The mobile-network material is especially important because a phone's radio environment is adversarial even when the application processor is correct. It documents that legacy/interoperability paths can expose null encryption, unauthenticated or non-integrity-protected signaling, tracking, and downgrade opportunities.

ENML therefore treats the modem/baseband and carrier network as a separate trust domain. Future radio policy must make downgrade and null-protection states explicit, minimize identity/location disclosure where the protocol permits, authenticate the local radio-control caller, keep raw modem control outside application authority, and surface meaningful protection-state changes through trusted system UI rather than silently pretending all generations/modes provide equivalent security.

### Cryptographic research and future algorithms

Merkle trees, post-quantum signature-compression work, traceable/linkable ring-signature research, and reduced-round hash cryptanalysis are retained as specialized references. None is automatically promoted into the ENML cryptographic base.

ENML does not invent a cryptosystem because a paper is interesting. A new primitive or mode requires a concrete ENML threat model, review of the security assumptions and failure modes, bounded implementation cost, migration/versioning rules, and interoperability/lifecycle justification. Reduced-round attacks are not treated as breaks of a full standardized primitive when the source itself does not make that claim.

### Hardware supply-chain material

The logic-locking paper is retained for its supply-chain threat framing and its emphasis on formal security definitions versus cycles of ad-hoc attack/defense. ENML does not copy logic-locking schemes into the phone design. The broader lesson is to state what a hardware trust mechanism protects, what it leaks, what manufacturing party it assumes, and what physical/fault/side-channel attacks remain outside the claim.

## Authority ranking

For current engineering decisions, the sandboxing/reference-monitor, mobile key-storage/encryption, mobile-network-security, cryptographic-boundary/key-lifecycle, and boot/update material have direct security-review relevance. The OS-structure and UI/UX documents are supporting conceptual guidance. Android architecture/backdoor/RAT material is primarily negative/threat evidence. The specialized signature/Merkle/logic-locking papers are retained for future threat-specific work rather than used to broaden today's trusted computing base.

When sources conflict, ENML does not choose the most feature-rich external mechanism. The deciding criteria remain the ENML threat model, least authority, fail-closed behavior, bounded resource use, low idle work, explicit ownership, recoverability without weakening security, and measured behavior on the target architecture.
