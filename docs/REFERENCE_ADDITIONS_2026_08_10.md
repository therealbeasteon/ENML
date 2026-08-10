# ENML supplied-reference additions — 2026-08-10

This document extends `REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md` with the additional source material supplied by the project owner on 2026-08-10.

## Strict source rule

**References teach principles. ENML determines implementation. External systems are not the design specification.**

For ENML architecture, security, kernel/runtime, UI/UX, power/performance, networking, cryptography, recovery and implementation-design decisions, the supplied ENML reference library is the allowed external guidance set.

Do **not** introduce design claims, security recommendations, architecture patterns, compatibility assumptions or implementation requirements from unrelated web pages, blog posts, vendor marketing, forums, unsupplied books/papers, or other external research unless the project owner explicitly authorizes a new source.

Even inside the allowed reference set, an external system's mechanism is not automatically ENML's mechanism. Extract the principle, failure mode, constraint or tradeoff; then derive the implementation from ENML's mission, threat model, resource/power targets, existing subsystem boundaries and measured behavior. ENML owns its ABI, service topology, wire formats, visual grammar, naming and compatibility decisions.

Repository code, repository tests, compiler/runtime diagnostics and measured behavior are evidence about ENML itself and remain valid engineering inputs. Third-party libraries may exist as private implementation dependencies, but their presence does not make their vendor architecture or public API a design reference for ENML. Public ENML contracts must continue to be derived from ENML requirements and the supplied reference set.

When a supplied historical reference conflicts with a newer supplied normative reference, prefer the newer supplied normative material and retain the older source only for historical/design context. If the supplied set does not establish a claimed compliance or protocol requirement, do not invent one.

## Additional supplied references

### `linux-kernel-slides(1).pdf` — Bootlin kernel/driver training

Use for Linux kernel implementation mechanics, kernel/userspace boundaries, drivers, memory constraints, synchronization and device integration. A particularly important lesson is that Linux internal driver APIs/ABIs are not stable across kernel releases; therefore ENML must keep Linux-internal details behind private adaptation/provider boundaries rather than freezing them into application ABI.

The material also reinforces that kernel memory/stack constraints differ from userspace and should be treated conservatively. Do not copy training examples as production policy.

### `the_design_of_the_unix_operating_system(1).pdf` — Maurice J. Bach

Use for enduring UNIX structure principles: kernel mediation, process/file abstractions, system-call boundaries, device insulation and clear separation between user programs and privileged mechanisms.

ENML is not a UNIX compatibility project. Borrow structure principles, not historical ABI, filesystem assumptions or implementation details.

### `William Stallings - Operating Systems (1)(1).pdf`

Use for process/thread models, scheduling, synchronization, memory, I/O, protection, virtualization, resource limits and general OS security reasoning. Capability partitioning and isolation examples are useful supporting material for ENML's bounded-principal/service architecture.

Treat period-specific mechanisms as examples rather than current standards.

### `an-213_duress_codes_in_protege_gx(1).pdf`

Use only as an operational example of how a real access-control product signals duress. It describes designated duress identities and PIN-plus-one style behavior that can silently notify a monitoring station.

This is **not** sufficient evidence for a coercion-resistant phone design. ENML must continue to pair this operational reference with the supplied panic-password threat-model material and must not ship a naive second-PIN/PIN+1 feature as if it solved repeated coercion.

### `2023-06-02-Mobile-Network-Security(1).pdf`

Use to maintain an explicit cellular/baseband threat model, including fake base stations/IMSI-catcher style tracking, interception, man-in-the-middle behavior, operator-message abuse and attacks against the SIM/baseband.

This reinforces the requirement that modem/baseband/radio remain a distinct trust boundary and that ENML application convenience must not collapse radio authority into the ordinary application domain.

### `L-0000567779-pdf.pdf` — *Symbian OS Internals: Real-time Kernel Programming* by Jane Sales et al.

Use as a major low-level mobile-OS reference for EKA2 kernel design, memory models/MMU/cache behavior, interrupts/traps/faults, platform security, capability model, data caging, file server, loader, window/input server, device drivers/HAL, power management, boot/sleep/wakeup, real-time behavior and performance.

Important ENML guidance from this source:

- keep the kernel/private substrate small and modular where practical;
- isolate file, window/input and other system services behind explicit ownership boundaries;
- design for resource-constrained mobile hardware rather than desktop abundance;
- keep important operations bounded and predictable where possible;
- separate platform-independent logic from board/ASSP/variant-specific adaptation;
- separate hardware-independent logical driver behavior from hardware-specific physical adaptation where that decomposition reduces porting cost;
- protect open application environments with memory protection, capability checks and data isolation;
- treat idle-time and power management as architectural concerns, not late optimization.

Do not reproduce EKA2 ABI, historical Symbian driver interfaces, active-object APIs, window-server contracts or exact scheduling behavior. ENML uses the concepts as guidance while preserving its own architecture and Linux-private substrate.

### `lf_linux_kernel_development_2010(1).pdf`

Use for Linux development-process and upstream-change context. The core ENML implication remains: do not make unstable Linux-internal interfaces part of public application ABI; isolate upstream churn behind private platform boundaries.

### `thesymbianosarchitecturesourcebook.pdf`

Use as a primary architecture reference for Symbian's design goals and patterns: minimal kernel responsibilities, client/server resource ownership, framework/plugin boundaries, hardware adaptation, event-driven applications, asynchronous services and robust resource-constrained design.

The source also reinforces that a mobile OS can keep an underlying platform architecture distinct from product-specific UI variants. ENML should preserve that separation while developing an original ENML visual language rather than cloning any Symbian UI.

### `Introduction to Assembly Language Programming_ From Soup to Nuts_(1).pdf`

Use for ARM instruction/register/calling/stack reasoning, machine-level debugging and transitions between assembly and higher-level code. Exact ENML startup/exception code must still match the chosen target architecture and ABI; educational examples are not themselves the platform contract.

## Previously supplied 2026-08-10 OS references

The following newly supplied files are also part of the allowed reference set and are recorded in `REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md`:

- `Operating Systems - Internals and Design Principles - 7th E.pdf`
- `Modern.Operating.Systems.2nd.Ed_.by_.Tanenbaum-not-scanned-1-1.pdf`
- `operating_systems_three_easy_pieces.pdf`
- `Operating Systems_ Principles and Practice, Vol. 1_ Kernels and Processes ( PDFDrive ).pdf`
- `book-riscv-rev3.pdf` — xv6/RISC-V teaching OS

These strengthen ENML's process isolation, policy-vs-mechanism, concurrency, virtualization, portability, trap/page-table and second-architecture reasoning. They remain guidance rather than compatibility targets.

## Development rule going forward

Every future ENML development slice should be defensible from:

1. the canonical `PROJECT_VISION.md` requirements;
2. the supplied reference library recorded by the reference documents;
3. existing ENML architecture/invariants;
4. repository tests, compiler/sanitizer diagnostics and measured runtime behavior.

The references constrain reasoning by teaching principles and failure modes; they do not choose ENML's implementation for us. If a design decision requires guidance that is absent from those inputs, stop short of inventing a new external authority. Record the gap and either choose a conservative ENML-local implementation that does not make unsupported claims, or wait for an explicitly supplied/authorized reference.
