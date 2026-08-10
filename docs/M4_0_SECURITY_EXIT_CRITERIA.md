# M4.0 — shell security and forensic-minimization exit criteria

## Authority

**References teach principles. ENML determines implementation. External systems are not the design specification.**

This checklist is derived from ENML's threat model and implementation invariants. The supplied hardening material reinforces system-owned access control and attack-surface reduction. The supplied Symbian architecture material reinforces explicit client/server ownership, asynchronous/event-driven service behavior and avoiding idle polling. The supplied mobile-security material reinforces confidentiality, integrity, availability and explicit threat modeling. Those references do not define ENML's wire protocol, service names, UI layout or ABI.

M4.0 may not leave draft until the shell satisfies the security properties below on the exact merge candidate.

## Privileged identity and authorization

- Trusted shell and secure-UI principals come from one canonical platform-principal definition; independent components may not silently invent substitute privileged identifiers.
- A numeric principal, ServiceId, ApplicationInstanceId, ProcessId, SurfaceId or session id is a label, **not** authority by possession.
- Every cross-process shell control path derives its caller from kernel packet credentials resolved through trusted runtime identity before privileged target state is used.
- Authorization should occur before target-dependent parsing/lookup where practical so unauthorized callers cannot use differential errors as an application/process/surface enumeration oracle.
- Shell and secure-UI authority remain distinct. Being one trusted system principal does not automatically grant the privileges of another.
- Ordinary applications receive no public API for mutating the shell task registry, minting trusted presentation, globally self-activating, or claiming shell/admin identity.
- There is no debug, recovery, test, factory, or development bypass compiled into the normal shell authority path that skips exact-principal checks.

## Lifecycle integrity and stale authority

- A shell task exists only when App Manager lifecycle identity and compositor application-root ownership corroborate the same exact live `PeerIdentity`.
- Lifecycle snapshots are bounded, revisioned, validated and contain no native PID, executable path, storage root, package directory handle or service capability.
- Duplicate application instance IDs, duplicate exact process identities, contradictory root surfaces and malformed trusted-presentation metadata fail closed.
- Compositor object IDs remain service-generation scoped. An object from a dead compositor generation never aliases a replacement object.
- Foreground activation binds shell revision + application instance + signed application identity + exact `PeerIdentity` + generation-scoped application root.
- Activation intent is revalidated immediately before the compositor commit. Lifecycle, navigation, owner or root-surface mutation makes an older intent stale.
- The compositor independently revalidates the exact expected application owner at activation commit. The shell cannot redirect one task identity to another application's surface through a stale/guessed `SurfaceId`.

## Attack-surface and resource discipline

- No global task scanner, `/proc` crawler, process polling loop, permanent thumbnail worker, permanent animation timer or unbounded navigation/event queue is introduced.
- The shell live-task ceiling remains bounded by the authoritative App Manager live-instance ceiling rather than creating a second hidden process registry.
- Lifecycle projection is demand/event driven. Repeated reads of unchanged live state do not churn revisions or create background work.
- Private shell control transports use bounded messages, reject unexpected handles/trailing bytes and fail explicitly on capacity/revision exhaustion.
- System chrome and future Home/Overview rendering use the existing bounded semantic UI/compositor contracts rather than direct raw framebuffer/input/filesystem authority.

## Privacy and forensic minimization

M4.0 does **not** claim that the whole device is forensic-proof. Full resistance to offline extraction depends on later verified boot, production hardware-backed key storage, recovery/update policy, encryption/key-erasure behavior and hardware integration. The shell must nevertheless avoid creating unnecessary forensic artifacts.

- Recent-task state is memory-only in M4.0; no recents database, navigation history file or hidden durable task journal is created merely for shell convenience.
- Task preview authorization is ephemeral and contains no pixels, mapped memory, path, file descriptor, cache key or persistence request.
- The default preview policy captures only an exact **currently visible application root** whose compositor entry explicitly allows capture, has no trusted presentation classification, and still presents the exact authorized frame.
- Hidden applications fall back to semantic overview cards rather than forcing screenshot retention solely to decorate recents.
- Popups, system chrome and secure-system presentation are never accepted as ordinary task-preview inputs.
- Preview grants are revision/frame/owner/surface bound and revalidated immediately before sampling; they are never serialized or treated as timeless bearer capabilities.
- No task preview is written to disk by the M4.0 shell foundation. Any future persistence proposal requires a separate threat model, encryption/key-lifecycle design and explicit product decision.
- Logs and diagnostics must not dump private surface contents, application text, cryptographic material, capability handles, raw authentication secrets or hidden shell history.

## Trusted presentation

- Applications cannot mint `system_chrome` or `secure_system` role by drawing lookalike pixels.
- Trusted-system attribution remains compositor-derived and technically separate from application-provided framebuffer content.
- Secure-system capture exclusion remains independent of visual styling.
- Future system-chrome lifecycle must fail closed across compositor restart and preserve exact shell principal ownership.

## Validation gate

Before M4.0 is mergeable as complete, the exact candidate must pass:

- shell GCC gate;
- shell Clang gate;
- shell ASan/UBSan gate;
- native AArch64 shell gate;
- display/compositor GCC, Clang, sanitizer and native AArch64 gates for shell-dependent compositor changes;
- unchanged lower milestone security/runtime gates affected by App Manager or common-core changes.

Security-negative tests must cover at least unauthorized lifecycle access, malformed lifecycle state, self-activation denial, exact-owner mismatch, stale compositor generation, stale shell activation intent, hidden/non-capturable/trusted preview denial, and stale preview-frame rejection.

## Explicit non-goals for this milestone

M4.0 does not fake later security properties. Verified boot/attestation, production hardware-backed key providers, full encrypted-recovery design, baseband/radio hardening, hardware DRM/KMS/GPU composition, final lock-screen authentication, duress handling and complete physical-forensic resistance remain separate milestones.

In particular, ENML must not implement a naive second "panic PIN" and label it coercion-resistant. The supplied duress reference demonstrates that simple two-password schemes have a narrow threat model and can fail under repeated coercion/forced-randomization.
