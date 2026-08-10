# ENML original project foundation references — 2026-08-09

This document records how the original project reference set is to guide ENML. The references are inputs to engineering judgment, threat modeling, interaction design and implementation discipline. They are not templates, compatibility targets, or permission to reproduce another operating system, vendor API, visual language or historical mechanism.

## Governing rule

ENML owns its architecture, wire contracts, security boundaries and visual identity.

A reference can contribute:

- a design principle;
- a threat or failure mode;
- an implementation technique;
- terminology useful for reasoning;
- a validation strategy;
- historical context.

A reference does not automatically contribute:

- its vendor ABI;
- its exact UI appearance;
- obsolete algorithms or protocol versions;
- application-visible Linux/kernel interfaces;
- legacy trust assumptions;
- a dependency merely because the reference used one.

## C++ and C# language references

### Bjarne Stroustrup — The C++ Programming Language, Fourth Edition

Use as a primary implementation-discipline reference for ENML C++ code:

- strong static typing and explicit domain types;
- resource ownership tied to scope;
- RAII and move-only handles for capabilities/resources;
- class and subsystem invariants;
- bounded, reviewable abstractions that do not require loss of runtime efficiency;
- explicit failure semantics.

ENML's no-exceptions/no-RTTI core profile is a project constraint, so exception-oriented examples are translated into explicit `Result`/error and invariant-preserving transaction patterns rather than copied literally.

### Jesse Liberty — Programming C#, Second Edition

Use conceptually where it helps reason about higher-level application models, events, threading/synchronization, streams, metadata/reflection and managed-runtime tradeoffs. It is not a decision to adopt the CLR, .NET ABI, Windows Forms, ASP.NET, remoting or COM as ENML platform dependencies.

## ARM and low-level implementation

### Charles W. Kann — Introduction to Assembly Language Programming: From Soup to Nuts, ARM Edition

Use as a low-level ARM reasoning reference for:

- instruction behavior;
- registers and calling conventions;
- stack/procedure discipline;
- machine-code level debugging;
- transition points between assembly and higher-level code.

This is educational ARM material rather than a source of ENML ABI. Platform startup code must still follow the exact target architecture, exception level, AAPCS64 and board/firmware contract used by ENML.

## Linux kernel and OS structure references

### Bootlin — Linux kernel and driver development training

Use for current Linux/kernel implementation knowledge, especially drivers, kernel/userspace boundaries, synchronization, memory, device integration and debugging. Linux mechanisms remain private substrate unless ENML deliberately promotes a semantic abstraction into stable platform ABI.

### Linux Foundation — Linux Kernel Development white paper

Use mainly for development-process context: the kernel is a fast-changing, large multi-party project. ENML should therefore avoid freezing unstable Linux implementation details into application ABI and should isolate the platform from upstream churn.

### Maurice J. Bach — The Design of the UNIX Operating System

Use for enduring OS-structure principles:

- kernel mediation of hardware;
- well-defined system-call boundaries;
- insulation of user programs from hardware idiosyncrasies;
- layering and separable system services.

ENML is not attempting binary or API compatibility with historical UNIX.

### William Stallings — Operating Systems: Internals and Design Principles, Seventh Edition

The newly added Seventh Edition copy makes the Stallings reference explicit. Use it for process/thread structure, concurrency, scheduling, memory management, protection/security, I/O and virtualization reasoning. Treat algorithms and examples as engineering evidence rather than current normative security guidance; ENML still owns its capability model, service decomposition and platform ABI.

### Operating System Concepts — Operating-System Structures chapter

Use for the separation of OS services, user interfaces, system calls, system programs, structure, debugging and boot. These are architectural categories, not a mandate to reproduce a particular textbook kernel organization.

### The little book about OS development / Operating System Development from Scratch

Use as implementation-learning material for boot, early kernel, interrupts, memory and simple system construction. These sources are not production-security specifications. ENML's production decisions must remain stricter than educational examples.

### Andrew S. Tanenbaum — Modern Operating Systems, Second Edition

Use as a historical systems-architecture reference for the OS as both an extended machine and a resource manager, and for processes, memory, I/O, files, embedded systems and synchronization. The edition is old enough that concrete platform/security recommendations must not be treated as current standards. Its enduring abstractions are useful; its period-specific implementation assumptions are not ENML requirements.

### Remzi H. Arpaci-Dusseau and Andrea C. Arpaci-Dusseau — Operating Systems: Three Easy Pieces

Use the virtualization/concurrency/persistence framing as a disciplined way to reason about resource ownership and measurable tradeoffs. Particularly useful ENML implications are:

- private virtual address spaces remain fundamental process-isolation machinery;
- concurrency must be justified and synchronized rather than added casually;
- policy should be separated from mechanism where practical;
- performance claims should be measured rather than inferred from abstraction alone.

OSTEP is a teaching/reference source, not an ENML ABI or kernel-layout template.

### Thomas Anderson and Michael Dahlin — Operating Systems: Principles & Practice, Volume I: Kernels and Processes, Second Edition

Use for protection, fault isolation, virtualization, resource allocation, concurrency and portability reasoning. The distinction between **security policy** (what is permitted) and **enforcement mechanism** (how it is enforced) is directly relevant to ENML's principal/capability architecture. The hardware-abstraction discussion also reinforces ENML's rule that board/device specifics remain below narrow private adaptation boundaries.

Do not copy its teaching kernel organization or platform-specific examples. ENML should preserve stable semantic interfaces while allowing the private hardware substrate to change.

### Cox, Kaashoek and Morris — xv6: a simple, Unix-like teaching operating system (RISC-V)

Use xv6 as a concrete code-level reference for understanding RISC-V privilege transitions, traps/system calls, process address spaces, page tables, scheduling, locks, interrupt handling and file-system recovery. It is especially useful as a second-architecture check against accidentally making ENML's private OS contracts ARM64-specific.

Important guardrails:

- xv6 is deliberately a small teaching system, not a production mobile-security architecture;
- its monolithic layout, process model, driver arrangement and syscall surface are not ENML compatibility targets;
- RISC-V `ecall`/`sret`, supervisor mode and Sv39 examples are architecture-specific mechanisms, while isolation, trap mediation and page-table ownership are the transferable principles;
- ENML's present native validation target remains AArch64, but portable private boundaries should leave room for another architecture without rewriting application semantics.

## Mobile and radio security

### Mobile Network Security — Mobile Security 2023

Use to keep the cellular/radio threat model explicit. Important objectives include mutual authentication, confidentiality of user/signaling data and identity/location, signaling integrity, privacy/untraceability goals, algorithm agility and careful reasoning about end-to-end versus hop-by-hop protection.

The modem/baseband/radio stack must remain a distinct trust and attack surface; application convenience must not erase that boundary.

### NIST SP 800-124 Revision 1

This uploaded revision is withdrawn and was superseded by SP 800-124r2 in May 2023. ENML therefore treats Revision 1 as historical design evidence only.

Still-useful principles include:

- explicitly model mobile-device threats before deployment;
- define architecture, authentication, cryptography, configuration and provisioning as separate security concerns;
- secure to a known-good state;
- test pilots and failure/fallback behavior;
- maintain, patch, audit and periodically reassess policy.

Current compliance or normative claims must use current standards, not this withdrawn revision.

## Storage encryption and cryptographic policy

### BitLocker Drive Encryption Security Policy for FIPS 140-2 validation

Use as historical evidence for disciplined cryptographic-module reasoning:

- define cryptographic boundaries;
- separate roles/services/authentication;
- make startup/recovery behavior explicit;
- document key generation, entry/output, distribution, zeroization and storage;
- define self-tests and integrity checks.

Do not copy historical BitLocker algorithms, Windows-specific boot architecture or obsolete FIPS assumptions into ENML. ENML's Key Service keeps key identity, protection hierarchy and provider internals separate for this reason.

## Duress and panic authentication

### Clark & Hengartner — Panic Passwords: Authenticating under Duress

This is a threat-model reference, not a feature recipe. The simple two-password model is vulnerable under repeated coercion and forced-randomization assumptions. Any future ENML duress feature must therefore begin with explicit adversary capabilities, persistence, observation, iteration and response behavior rather than shipping a naive 'second PIN'.

### ICT AN-213 — Duress Codes in Protege GX

Use as an implementation/operational example of duress signaling in a real access-control product. It does not override the academic threat-model caveats above and does not define ENML authentication UX or protocol.

## BlackBerry architecture and UI references

### BlackBerry Architecture

Use for lessons from constrained mobile systems:

- power/resource limits matter to architecture;
- application faults should not compromise radio/system code;
- isolation boundaries matter even when hardware is constrained;
- mobile security cannot depend on application correctness.

Do not copy the historical JVM/application architecture or vendor APIs.

### BlackBerry Smartphones 7.1 UI Guidelines

Use for task-oriented workflows, visible state, focus, feedback, recoverability, localization, scalable text, accessibility and disciplined mobile interaction. Do not copy components, iconography, typography, theme assets or BlackBerry visual identity.

## One UI and mobile UX references

### Samsung One UI Design Guidelines

Use for ergonomic/reachability reasoning, responsive composition, accessibility, interaction hierarchy and the general principle that a coherent system should help users concentrate on important content.

Do not copy Android structure, One UI components, large-radius focus blocks, iconography, typography, motion signatures or vendor visual grammar. ENML's visual language remains original, classic/crafted, dimensional, colorful, translucent where appropriate and characterized by its own authored contours and motion.

### Mobile app UI/UX and UX-process material

Use for discovery, user goals, workflow mapping, wireframes, prototypes, usability evaluation, reusable components and developer handoff. ENML does not treat a mockup as architecture: semantics, accessibility, trust, layout and renderer capability remain independent layers.

## Current implications for M3.2 and later platform work

The reference set reinforces the current direction:

1. Application pixels are not trusted UI authority.
2. Secure-system attribution must be minted by a trusted compositor/service boundary, not by a style token an application can request.
3. The semantic UI tree remains separate from raw graphics, Linux devices and vendor component APIs.
4. Renderer inputs stay strongly typed, bounded and invariant-checked.
5. Rich ENML material/lighting/translucency must degrade without erasing focus, hierarchy or security state.
6. Mobile/radio, storage/key, display/compositor and application authorities remain distinct trust domains.
7. A future duress path requires its own reviewed threat model and must not be reduced to a second-password trick.
8. Historical or superseded sources are never used to make current compliance claims.
9. Process isolation, virtual memory, trap mediation and resource ownership should remain explicit even when Linux currently supplies the private kernel substrate.
10. Policy/enforcement and hardware/semantic boundaries should remain separable enough that a later board, kernel or architecture port does not force application-ABI redesign.
11. Concurrency is not a default solution: ENML continues to prefer event-driven/on-demand work and introduces threads only when there is a measurable reason.
12. Educational kernels such as xv6 are used to validate understanding of mechanisms, never as shortcuts around ENML's stronger mobile security and lifecycle requirements.

## Reference-update policy

When a newer authoritative source supersedes an older reference, preserve the older source for design history but mark its status clearly. New code should prefer current normative guidance where compliance, cryptography, platform security or protocol correctness is involved.
