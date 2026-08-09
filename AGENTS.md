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
- Application launch is package-based, never arbitrary-path-based. Apps do not choose executable paths, native credentials, `PrincipalId`, active generation, content digest, storage root, or sandbox policy.
- Staging a package generation does not activate it. Running processes stay pinned to the immutable generation that created them.
- Per-user application PrincipalIds are durable identities and are not recycled casually across update/uninstall/reinstall.
- Uninstall is not synonymous with data/key destruction. Launch state, process authority, immutable code retention, principal history, private data, backup state, and keys are separate resources.
- Public app storage APIs expose neither Linux fd numbers nor absolute Linux paths as stable ABI.
- Storage traversal stays rooted in trusted object authority; never reintroduce caller-controlled absolute `open()` paths.
- Storage root selection is based on trusted `RequestContext.peer`; public Storage requests never claim PrincipalId/UserId/native identity.
- Storage object rights are authoritative on the server. Client-side rights checks are ergonomic only.
- Object delegation may only reduce rights.
- RPC error responses never transfer handles. Successful handle-bearing messages must keep flags/count/SCM_RIGHTS consistent.

## Completed implementation

- M0.0-M0.10: build/oscore, bounded OSIP/Channel, OSIDL, typed Echo, supervisor lifecycle, trusted identity, Linux sandbox, adversarial/resource gates, native AArch64 and independent cross/QEMU validation.
- M1.0-M1.5: signer-bound package identity, hostile bounded manifests, durable staging/activation, generation-bound App Manager launch, durable per-user principals/application sandbox, update/uninstall/revocation/generation retention.
- M2.0: descriptor-rooted private storage with bounded UTF-8 `RelativePath`, typed `File`/`Directory`, `O_NOFOLLOW` confinement and crash-resistant atomic replace.
- M2.1: identity-rooted Storage Service, typed directory/file object capabilities, bounded handle transfer, server-authoritative rights reduction and adversarial identity tests.

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, `docs/M2_STATUS.md`, `docs/M2_0_PRIVATE_STORAGE.md`, and `docs/M2_1_STORAGE_SERVICE.md` before changing those substrates.

## Current milestone: M2.2 Storage product integration

M2.2 cuts applications over from direct private-data directory authority to a supervised Storage Service.

Current product path:

1. App Manager resolves package owner, active immutable generation, durable per-user PrincipalId and retained private-data profile from trusted state.
2. App Manager publishes `(PrincipalId, UserId) -> already-authorized private root` over the Storage service's private supervisor control channel.
3. A launched application receives a connected Storage Service endpoint in Linux-private bootstrap fd 5, not a private-data directory.
4. App Manager registers the child process with the supervisor before sending application bootstrap/READY state.
5. The app calls `StorageClient::open_private_root()`; Storage resolves per-message kernel credentials through the trusted identity registry and selects the root from `RequestContext.peer`.
6. Storage returns typed local object capabilities with server-held rights.

`application_storage_service_fd` is a private bootstrap transport slot, not public ABI. Test fixtures must prove it is an IPC socket rather than a data-directory descriptor.

Application Landlock may now use `private_data_directory_fd = -1`; in that mode the process receives no direct writable private-data tree. Private data is reached only through Storage objects.

The private Storage control protocol may carry a target PrincipalId/UserId only because possession of that private supervisor control channel is system authority. The public Storage endpoint must never gain an equivalent operation.

Storage service restart semantics are explicit:

- object endpoints minted by the dead generation stay dead and return `peer_died`;
- no stale bearer capability is silently reconnected;
- App Manager retains trusted profile state and republishes roots to the new Storage generation;
- a new process registration gets a fresh `ProcessId`, while the durable application PrincipalId/UserId remains stable.

The current `Supervisor&` used by App Manager is a vertical-slice control plane for `system.storage`. Do not proliferate one service-specific supervisor dependency per future API; a general service directory/control plane is later work.

## M2.0/M2.1 invariants still apply

- `RelativePath` is fixed-capacity UTF-8 and rejects absolute paths, empty segments, `.`/`..`, NUL, backslash, malformed UTF-8 and overlong forms.
- `PrivateRoot` originates only from an already-authorized directory handle.
- Descendant traversal is descriptor-relative, segment-by-segment and `O_NOFOLLOW`.
- Final `File` objects must be regular files; FIFOs/sockets/devices/directories are rejected.
- `atomic_replace()` remains bounded same-directory temp/write/fsync/rename/parent-fsync.
- Root ownership key is durable `PrincipalId + UserId`, not `ProcessId`.
- Object endpoints are bearer capabilities and rights stay server-side.

## CI boundary

M0 is a frozen foundation gate. M0 CTests carry the explicit `m0` label; the M0 workflow selects that label rather than accidentally running M1/M2 tests. The cross/QEMU gate additionally excludes supervisor/Landlock tests that require native kernel process semantics. Native AArch64 remains the authoritative full-kernel behavior gate.

Do not "fix" an M0 QEMU failure by weakening a later M2 test. First verify whether the test is actually qemu-user-safe.

## References

Read `docs/REFERENCE_NOTES_2026_08_08.md` before architecture-sensitive changes. References are design evidence, not instructions to copy old vendor APIs, obsolete crypto suites, historical Symbian ABI details, or educational kernel architectures.

## Next after M2.2

M2.3 should harden resource ownership and revocation before encryption/key work:

- per-principal object quotas rather than only a global object table;
- bounded outstanding I/O/byte accounting;
- deterministic object cleanup on identity/root revocation;
- root revocation invalidates existing object slots for that profile;
- adversarial tests for service death during operations, revoked principals, uninstall/reinstall and quota exhaustion;
- preserve the rule that no raw private-data directory reaches an app.

Do not jump to document/media sharing, UI, telephony, or Key Service until M2.2 is merged and M2.3 resource/revocation semantics are settled.

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

M2.2 focused gates must run on GCC, Clang and native AArch64. Process-supervision Storage tests are not qemu-user-safe and belong to M2/native execution, not the M0 cross/QEMU test set.
