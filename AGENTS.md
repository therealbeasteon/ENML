# ENML OS — Codex Working Instructions

This repository is an incremental implementation of ENML OS. Continue the existing architecture; do not redesign it into Android, a conventional desktop Linux distribution, a monolithic daemon, or a from-scratch kernel.

## Product direction

ENML aims for appliance-like phone behavior, small trusted components, strong process isolation, bounded IPC, stable public APIs, low idle work, fast boot/resume, and a modern phone UX. Linux is the private hardware/process kernel. ENML services, OSIDL, security identities, package/storage/UI/telephony APIs are the public OS personality.

## Hard architectural constraints

- ARM64 is the primary target; x86-64 Linux remains a host implementation tier.
- Linux PID/UID/GID are implementation evidence, never public ENML identity.
- `PrincipalId`, `UserId`, and logical `ProcessId` come from trusted system state; request payloads never establish caller identity.
- Public IPC uses explicit little-endian serialization. Never serialize native C++ object layout.
- `WireHeaderV1` is a 40-byte logical format. Inline payload remains bounded to 64 KiB and transferred handles to 16 unless a later reviewed ABI revision changes it.
- Handles/native descriptors are move-only RAII. Descriptor inheritance is deny-by-default.
- No shell execution for services and no runtime YAML/JSON/XML parser in the supervisor.
- No universal "system UID" authority model.
- No exceptions across IPC boundaries. Core/system runtime stays no-exceptions/no-RTTI where currently configured.
- Keep normal service hot paths bounded; no hidden thread pools, unbounded queues, or accidental polling loops.
- Generated OSIDL code is ABI/convenience machinery, not the authorization boundary.
- Application launch is package-based, never arbitrary-path-based. Apps do not choose executable paths, native credentials, `PrincipalId`, active generation, content digest, storage root, key identity, or sandbox policy.
- Staging a package generation does not activate it. Running processes stay pinned to the immutable generation that created them.
- Per-user application PrincipalIds are durable identities and are not recycled casually across update/uninstall/reinstall.
- Uninstall is not synonymous with data/key destruction. Launch state, process authority, immutable code retention, principal history, private data, backup state, and keys are separate resources.
- Public app storage APIs expose neither Linux fd numbers nor absolute Linux paths as stable ABI.
- Storage traversal stays rooted in trusted object authority; never reintroduce caller-controlled absolute `open()` paths.
- Storage root selection is based on trusted `RequestContext.peer`; public Storage requests never claim PrincipalId/UserId/native identity.
- Storage object rights are authoritative on the server. Client-side rights checks are ergonomic only.
- Object delegation may only reduce rights.
- Revoking a Storage profile invalidates all already-minted object endpoints for that `PrincipalId + UserId`.
- RPC error responses never transfer handles. Successful handle-bearing messages must keep flags/count/SCM_RIGHTS consistent.
- Long-lived cryptographic keys must never become public raw-byte application APIs.

## Completed implementation

- M0.0-M0.10: build/oscore, bounded OSIP/Channel, OSIDL, typed Echo, supervisor lifecycle, trusted identity, Linux sandbox, adversarial/resource gates, native AArch64 and independent cross/QEMU validation.
- M1.0-M1.5: signer-bound package identity, hostile bounded manifests, durable staging/activation, generation-bound App Manager launch, durable per-user principals/application sandbox, update/uninstall/revocation/generation retention.
- M2.0: descriptor-rooted private storage with bounded UTF-8 `RelativePath`, typed `File`/`Directory`, `O_NOFOLLOW` confinement and crash-resistant atomic replace.
- M2.1: identity-rooted Storage Service, typed directory/file object capabilities, bounded handle transfer, server-authoritative rights reduction and adversarial identity tests.
- M2.2: supervised `system.storage`, App Manager profile publication/republication, no direct private-data directory in apps, generation-bound Storage capabilities and restart behavior.
- M2.3: per-profile object quotas, quota isolation by `PrincipalId + UserId`, deterministic root/object revocation, uninstall Storage-policy revocation while retaining data/principal continuity, and fresh capability reacquisition on reinstall/re-enable.

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, `docs/M2_STATUS.md`, `docs/M2_0_PRIVATE_STORAGE.md`, `docs/M2_1_STORAGE_SERVICE.md`, `docs/M2_2_STORAGE_PRODUCT_INTEGRATION.md`, and `docs/M2_3_STORAGE_REVOCATION_AND_QUOTAS.md` before changing those substrates.

## Storage invariants

- `RelativePath` is fixed-capacity UTF-8 and rejects absolute paths, empty segments, `.`/`..`, NUL, backslash, malformed UTF-8 and overlong forms.
- `PrivateRoot` originates only from an already-authorized directory handle.
- Descendant traversal is descriptor-relative, segment-by-segment and `O_NOFOLLOW`.
- Final `File` objects must be regular files; FIFOs/sockets/devices/directories are rejected.
- `atomic_replace()` remains bounded same-directory temp/write/fsync/rename/parent-fsync.
- Root ownership and Storage quota keys are durable `PrincipalId + UserId`, not `ProcessId`.
- Storage object endpoints are bearer capabilities and rights stay server-side.
- One profile is limited to 16 live Storage object slots beneath the 64-slot global table.
- Root revocation removes future root lookup and closes every already-minted object endpoint for the profile.
- Republish/reinstall never resurrects an old object endpoint; callers reacquire fresh capabilities.
- App Manager may retain private data and durable principal state after uninstall while live Storage authority remains revoked.
- Current Storage I/O is synchronous/single-threaded and per-operation bounded. Add outstanding byte/request budgets when concurrent/asynchronous Storage queues are introduced, not as inert counters today.

## CI boundary

M0 is a frozen foundation gate. M0 CTests carry the explicit `m0` label; the M0 workflow selects that label rather than accidentally running M1/M2 tests. The cross/QEMU gate additionally excludes supervisor/Landlock tests that require native kernel process semantics. Native AArch64 remains the authoritative full-kernel behavior gate.

Do not "fix" an M0 QEMU failure by weakening a later M1/M2 test. First verify whether the test is actually qemu-user-safe. M1 and M2 have their own GCC, Clang and native-AArch64 gates.

## References

Read `docs/REFERENCE_NOTES_2026_08_08.md` before architecture-sensitive changes. References are design evidence, not instructions to copy old vendor APIs, obsolete crypto suites, historical Symbian ABI details, educational kernel architectures, or legacy cellular security mechanisms.

For kernel/BSP work, preserve an upstream-first Linux strategy and small reviewable patches. For C++ core code, prefer type-rich lightweight abstractions, RAII, deterministic ownership and moves. For encryption design, borrow key-hierarchy/boot-integrity principles from historical full-disk-encryption systems without copying their obsolete algorithm choices.

## Current next milestone: M2.4 Key Service foundation

M2.4 begins the userspace cryptographic authority layer. Keep it narrow and testable; do not jump directly to hardware-backed attestation or full verified boot.

Required first slice:

1. Define stable opaque key identifiers and move-only key/session handles. No public raw long-lived key bytes.
2. Add a bounded `system.keys` service with identity-authenticated OSIP requests.
3. Separate permission to request a key operation from possession of a specific key handle/right.
4. Model a logical hierarchy suitable for system/profile/application keys, but keep TPM/TEE/HSM details behind a provider interface.
5. Add a software-backed test provider only for host/CI; do not mistake it for production hardware protection.
6. Define key creation/import policy narrowly. Applications must not choose another principal's key owner in a public payload.
7. Support version metadata and rotation without changing a durable logical key identity.
8. Add explicit destroy/cryptographic-erasure semantics distinct from package uninstall or file deletion.
9. Use authenticated encryption under a current reviewed cryptographic profile; do not invent a cipher or copy historical BitLocker suites.
10. Add adversarial tests: forged owner identity, stale handle, rights escalation, destroyed key, wrong key version, malformed payload, process death, and service restart.

Keep boot sealing, measured boot, recovery policy, hardware-root selection and attestation as later hardware/BSP integration work unless a minimal provider contract is needed to avoid painting the API into a corner.

## Build and test

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset host-asan
cmake --build --preset host-asan
ctest --preset host-asan
```

For Clang:

```sh
cmake -S . -B build/host-clang -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build/host-clang
ctest --test-dir build/host-clang --output-on-failure
```

M2/M2.4 process-sensitive tests must run on GCC, Clang and native AArch64. Do not add them to the M0 qemu-user-safe set unless they are explicitly proven safe there.
