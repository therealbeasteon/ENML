# ENML OS — achievement checklist

## Implementation authority

**References teach principles. ENML determines implementation. External systems are not the design specification.**

This document is a running ledger of what ENML has actually built and validated.
It is not a roadmap and not a wishlist. An item is checked only when it exists in
the tree and its gates are green on the merge candidate.

## How to read this

- `[x]` — implemented and validated by a green gate.
- `[ ]` — not implemented, or implemented without the gate that would justify it.
- **Not claimed** sections are as important as the checked ones. ENML's habit of
  writing down what it has *not* earned is what keeps the checked items
  meaningful.

Each milestone's authoritative detail lives in its own status document; this
file is the index, not a replacement.

---

## M0 — trusted foundation

`docs/M0_STATUS.md`

- [x] **M0.0** Build and repository foundation; warnings-as-errors with
      `-Wconversion`/`-Wsign-conversion` across GCC and Clang.
- [x] **M0.1** `liboscore`: `Result<T>`, `Error`, strong IDs, checked
      arithmetic, move-only RAII `NativeHandle`.
- [x] **M0.2** Bounded `libosipc` wire codec. Explicit little-endian, 40-byte
      `WireHeaderV1`, 64 KiB payload ceiling, 16-handle ceiling, unknown flags
      and nonzero reserved fields rejected, canonical booleans, complete UTF-8
      validation (overlongs, surrogates, above U+10FFFF).
- [x] **M0.3** Linux `AF_UNIX`/`SOCK_SEQPACKET` transport. `SCM_RIGHTS`
      descriptor passing, `FD_CLOEXEC` on receipt, `SO_PASSCRED` per-message
      credentials, strict ancillary validation, peer-death semantics.
- [x] **M0.4** `osidlc` compiler with deterministic lexer/parser/semantic pass
      and generated types, codecs and ABI metadata. No runtime schema parser in
      services.
- [x] **M0.5** Typed cross-process Echo RPC with generated client and dispatcher
      bindings, request-ID validation and a canonical error envelope.
- [x] **M0.6–M0.7** Supervisor process lifecycle, readiness, crash-loop
      suppression, restart policy, and trusted identity resolution separating
      kernel evidence from ENML `PrincipalId`.
- [x] **M0.8** Linux sandbox: `no_new_privs`, capability clearing, seccomp,
      Landlock, resource limits.
- [x] **M0.9** Adversarial and resource-limit gates against hostile services.
- [x] **M0.10** Native AArch64 gate plus an independent cross-build/QEMU gate.
- [x] Zero dynamic allocation proven on the IPC and typed-RPC hot paths.

**Not claimed at M0:** any kernel of ENML's own. Linux is the private
hardware/process substrate.

---

## M1 — packages, applications, principals

`docs/M1_STATUS.md`

- [x] **M1.0** Signer-bound package identity and bounded hostile-input manifest
      analysis.
- [x] **M1.1–M1.2** Manifest analyzer and durable package registry.
- [x] **M1.3** Generation-bound App Manager launch. Applications never choose
      executable path, credentials, `PrincipalId`, generation, digest, storage
      root, key identity or sandbox policy.
- [x] **M1.4** Durable per-user application principals and per-application
      sandbox.
- [x] **M1.5** Update, uninstall, revocation and generation retention, with
      running processes pinned to the immutable generation that created them.

---

## M2 — storage, keys, service composition

`docs/M2_STATUS.md`

- [x] **M2.0** Descriptor-rooted private storage. Bounded UTF-8 `RelativePath`,
      `O_NOFOLLOW` segment-by-segment traversal, crash-resistant atomic replace.
- [x] **M2.1** Identity-rooted Storage Service with typed object capabilities
      and server-authoritative rights reduction.
- [x] **M2.2** Supervised `system.storage`; no direct private-data directory in
      applications.
- [x] **M2.3** Per-profile quotas, deterministic revocation, uninstall policy
      revocation with data and principal continuity preserved.
- [x] **M2.4–M2.5** Typed Key Service AEAD path (AES-256-GCM-v1), opaque
      `KeyId`s, bounded `EKEY` envelopes, logical key rotation with retained
      historical versions.
- [x] **M2.6** Provider-wrapped persistence, durable `KRG1` registry with
      canonical binding, transactional publication, durable tombstones.
- [x] **M2.7** System → profile → application protection scopes with strict
      downward-only hierarchy.
- [x] **M2.8** Supervised `system.keys` with application lifecycle key policy
      and generation-aware policy replay.
- [x] **M2.9** Shared pidfd-backed boot-scoped `ProcessAuthority` and bounded
      trusted `ServiceBroker`; one `PeerIdentity` across Storage and Keys.
- [x] **M2.10** Long-lived private `PlatformServiceSession` with exact runtime
      credential validation and explicit fresh endpoint reacquisition.

**Not claimed at M2:** production TPM/TEE/HSM providers, and anti-rollback.
`MonotonicSecurityState` is an interface boundary only; the OpenSSL provider and
fixed wrapping root are test-only.

---

## M3 — display, compositor, semantic UI

`docs/M3_STATUS.md`

- [x] **M3.0** Display and compositor foundation with generation-scoped object
      identity.
- [x] **M3.1** Shared-buffer compositor service.
- [x] **M3.2** Semantic UI foundation: semantic tree, responsive layout,
      collections, render damage planning, contour anti-aliasing, paragraph
      layout, grapheme-safe font fallback, command-to-pixel text rendering.
- [x] **M3.2** Accessibility bridge and bounded cross-process accessibility and
      collection transports.
- [x] **M3.2** Trusted presentation: compositor-derived attribution that
      applications cannot mint by drawing lookalike pixels.
- [x] **M3.2** Input routing, delivery and hit-testing with authority checks.

**Not claimed at M3:** GPU/DRM/KMS composition, editable text and IME, color
font policy, final font assets and licensing, multitouch and gesture contracts.

---

## M4 — trusted phone shell and system quality

`docs/M4_0_TRUSTED_PHONE_SHELL_FOUNDATION.md`,
`docs/M4_0_SECURITY_EXIT_CRITERIA.md`

- [x] **M4.0** Bounded trusted `ShellTaskModel` with corroborated lifecycle
      authority: a task exists only when App Manager lifecycle identity and
      compositor application-root ownership agree on the same exact live
      `PeerIdentity`.
- [x] **M4.0** Forensic minimization: recent-task state is memory-only, preview
      grants are revision/frame/owner/surface bound and never serialized, no
      task preview is written to disk.
- [ ] **M4.1** Supervised phone shell and trusted chrome lifecycle — open in
      PR #28, gates green, not yet merged.
- [x] **M4.2** Measured resource budget gate. Resident set, time-to-ready and
      idle wakeups enforced against reviewed ceilings on native x86-64 and
      native AArch64. `docs/M4_2_RESOURCE_BUDGETS.md`
- [x] **M4.3** `Result` alternative misuse fails closed. `invariant_violated()`
      traps unconditionally instead of `assert()`, so the guard survives
      `NDEBUG`; a fork-based test proves each misuse path terminates.
- [x] **M4.4** Ancillary rejection closes unadopted descriptors rather than
      leaving kernel-installed descriptors unowned.
- [x] **M4.5** Fuzzing depth: seeded corpora, dictionaries and a nightly
      accumulating corpus, replacing a 1000-execution smoke run that could not
      clear the `OSIP` magic check. `docs/M4_5_FUZZING_DEPTH.md`
- [x] **M4.6** Private-storage confinement parser fuzzed, including an
      idempotence check that catches `parse()`/`view()` divergence — a
      confinement break with no memory-safety error attached.
- [x] **M4.7** Budget coverage extended to `system.storage`, ceilings calibrated
      from measured values, and measurements published as workflow annotations
      so a gate result is diagnosable without permission to download raw logs.
- [x] **M4.7** `system.storage` polls only live descriptors. It previously
      handed `poll()` the full 65-entry table capacity, which fails with EINVAL
      once `nfds` exceeds `RLIMIT_NOFILE` — and the default sandbox caps open
      files at 32. The service could not run under its own default hardening
      profile.
- [x] **M4.7** `system.storage` and `system.keys` block on a single wait set.
      Both previously polled control with a zero timeout and then dispatched
      with a 10 ms timeout; neither wait could block, so an idle service woke
      roughly 100 times a second forever. Measured 97/s before, 0/s after.
- [ ] **M4.x** Leak detection enabled on the fuzz targets (blocked on triaging
      `osidlc` allocation behavior).
- [ ] **M4.x** `system.keys` covered by a measured budget. Its idle fix is
      argued from symmetry with the verified storage case, not from a
      measurement; the service is gated behind `EMNL_BUILD_OPENSSL_TEST_PROVIDER`
      and needs a state directory descriptor.
- [ ] **M4.x** `KRG1` registry decoder fuzzed (blocked on a design decision:
      decode/IO separation versus a filesystem-backed harness).

---

## Mission scorecard

`docs/PROJECT_VISION.md` ranks nine priorities. This is where each one actually
stands, independent of milestone numbering.

| # | Priority | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Security by default and strong hardening | Strong, above the hardware line | Capability model, server-side rights, deterministic revocation, sandbox, adversarial tests, fail-closed `Result` |
| 2 | Low resource consumption, small trusted components | Now measured | M4.2/M4.7 budget gate over echo and storage; zero-allocation hot paths |
| 3 | Performance, fast startup, responsiveness | Partially measured | Services reach READY in 1–2 ms; no whole-system boot budget yet |
| 4 | Modular subsystem boundaries with explicit ownership | Strong | Per-subsystem `core/` libraries, typed service boundaries, `AGENTS.md` invariants |
| 5 | Efficient, secure, developer-friendly APIs | Strong | OSIDL build-time generation, no runtime schema parser |
| 6 | Hardware portability and clean adaptation boundaries | Partial | Native AArch64 and cross/QEMU gates; no board bring-up or driver model |
| 7 | Low idle activity and power efficiency | Measured, and it found real regressions | Idle-wakeup ceiling caught storage and keys spinning at ~100 Hz; both now measure 0/s |
| 8 | Excellent, accessible phone UX | In progress | Semantic UI, accessibility bridge, trusted presentation |
| 9 | Long-term maintainability and evolvability | Strong | Per-milestone invariant documents, exit criteria, honest non-goal records |

---

## Not claimed anywhere yet

These are the gap between a hardened userspace and a secure phone. They are
listed together because each is a whole workstream, and because leaving them
implicit is how a project starts believing its own marketing.

- [ ] Verified boot and attestation. Everything above it is defense a bootkit
      walks past.
- [ ] Production hardware-backed key storage (TPM/TEE/HSM) and hardware-rooted
      anti-rollback.
- [ ] Baseband and radio isolation. The modem is the largest remote attack
      surface on a phone and nothing in the tree addresses it.
- [ ] Telephony and RCS.
- [ ] Board bring-up, driver model and power management integration.
- [ ] Lock-screen authentication, and a coercion-resistance design that is not a
      naive second "panic PIN" — the supplied duress reference shows why simple
      two-password schemes fail under repeated coercion.
- [ ] Recovery, update and encrypted-backup policy.
- [ ] Application distribution.
- [ ] Whole-system boot-to-shell budget.
- [ ] Independent security review of the crypto and identity paths. Every line
      of the key hierarchy, `EKEY` envelope and `KRG1` format has had exactly
      one set of eyes.

---

## Maintaining this file

Check an item in the same change that earns it, and record the gate that proves
it. If a milestone's exit criteria are relaxed, unchecking here is part of that
change — a ledger that only ever moves forward stops being evidence.
