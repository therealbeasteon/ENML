# EMNL OS — Codex Working Instructions

This repository is an incremental implementation of EMNL OS. Continue the existing architecture; do not redesign it into Android, a conventional desktop Linux distribution, a monolithic daemon, or a from-scratch kernel.

## Product direction

EMNL aims for appliance-like phone behavior, small trusted components, strong process isolation, bounded IPC, stable public APIs, low idle work, fast boot/resume, and a modern phone UX. Linux is the private hardware/process kernel. EMNL services, OSIDL, security identities, package/storage/UI/telephony APIs are the public OS personality.

## Hard architectural constraints

- ARM64 is the primary target; host x86-64 Linux is the current implementation tier.
- Linux PID/UID/GID are implementation evidence, never public EMNL identity.
- `PrincipalId`, `UserId`, and logical `ProcessId` are resolved from trusted system state; application payloads never establish them.
- Caller identity must never be accepted from request payload fields.
- Public IPC uses explicit little-endian serialization. Never send native C++ structs as wire data.
- `WireHeaderV1` is a 40-byte logical format. Inline payload remains bounded to 64 KiB and transferred handles to 16 during M0/M2.1.
- Handles and native descriptors are move-only/RAII. Descriptor inheritance is deny-by-default.
- No shell execution for services. No YAML/JSON/XML parser in the supervisor.
- No universal "system UID" authority model.
- No exceptions across IPC boundaries. Core/system runtime is built with no exceptions/RTTI where currently configured.
- Keep normal service hot paths bounded; do not introduce hidden thread pools or unbounded queues.
- Generated OSIDL code is convenience and ABI machinery, not the authorization boundary.
- Preserve all existing tests before adding functionality.
- Application launch is package-based, never arbitrary-path-based. Apps must not choose Linux executable paths, native credentials, `PrincipalId`, active generation, content digest, data root, or sandbox policy.
- A staged package generation is not active merely because it exists. Activation affects future launches only; a running process stays bound to the generation that created it.
- Per-user application PrincipalIds are durable identities. Do not recycle them casually on update/uninstall/reinstall.
- Uninstall is not synonymous with data/key destruction. Package launch state, process authority, immutable code retention, application principal history, private data, backup state, and future cryptographic keys are separate resources.
- Public app storage APIs must not expose Linux fd numbers or absolute Linux paths as stable application ABI.
- Storage traversal must remain rooted in trusted object authority; do not reintroduce `open()` on caller-controlled absolute paths.
- Storage Service root selection is based on trusted `RequestContext.peer`, not PackageId/PrincipalId/user/native-fd claims in payloads.
- Storage object rights are authoritative on the server. Client-side rights checks are ergonomic only and may never be the security boundary.
- Object delegation may only reduce rights. Never widen a child capability because a caller supplied a larger mask.
- RPC error responses never transfer handles. Successful handle-bearing responses must keep `HAS_HANDLES`, `handle_count`, and actual SCM_RIGHTS descriptors consistent.

## Completed implementation

M0.0-M0.10 are complete: build/oscore, bounded OSIP codec/Channel, OSIDL, typed Echo, supervisor lifecycle, trusted identity, Linux sandbox, adversarial/resource gate, and ARM64 native/cross validation.

M1.0-M1.5 are complete and merged: signer-bound package identity, hostile manifest analysis, durable staging/activation, trusted generation-bound App Manager launch, durable per-user app principals/private-data sandbox, and update/uninstall/revocation/generation-retention semantics.

M2.0 is complete and merged: bounded descriptor-rooted private storage, `RelativePath`, typed move-only `File`/`Directory`, `O_NOFOLLOW` confinement, regular-file-only opens, stable storage errors, bounded positional I/O, and crash-resistant atomic replacement.

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, `docs/M1_5_UPDATE_UNINSTALL.md`, `docs/M2_0_PRIVATE_STORAGE.md`, and `docs/M2_STATUS.md` before changing those substrates.

## Current branch milestone: M2.1 Storage Service + typed object capabilities

M2.1 puts a service/object-capability boundary in front of M2.0.

The main Storage Service endpoint derives private-root authority from trusted `RequestContext.peer` and then looks up `(PrincipalId, UserId)` in trusted policy. `ProcessId` is not the ownership key, because process restarts must not create a new private-data domain.

The request payload for opening private storage contains no package id, principal id, uid/gid, native PID, fd number, or Linux path.

`Storage` uses service id `0x0000F020`; private object endpoints use `0x0000F021`. `0x0000F010` is already reserved by application bootstrap and must not be reused.

Successful RPC responses may now transfer bounded SCM_RIGHTS handles. `InboundMessage::take_handle()` is infrastructure-only ownership transfer; application-facing APIs immediately wrap received endpoints in typed move-only objects.

`DirectoryObjectHandle` and `FileObjectHandle` are dedicated local object endpoints. They do not serialize a native descriptor value. Server-side object slots retain the authoritative semantic type and rights mask.

Directory rights currently cover open/create/remove/atomic-replace operations. File rights currently cover read/write/stat/sync. A raw OSIP request that attempts to widen a child directory's rights must fail even if it bypasses the typed client API.

Object endpoints are bearer capabilities: possession is authority. This differs from the main service connection, where the caller is authenticated per message. Deliberate future cross-process delegation must preserve server-side rights reduction.

Synchronous M2.1 storage operations stay bounded inside the 64 KiB inline transport. Large streaming/shared-buffer I/O is not implemented by hiding an unbounded worker pool.

See `docs/M2_1_STORAGE_SERVICE.md`.

## M2.0 path/data-caging invariants still apply

- `RelativePath` remains fixed-capacity UTF-8 and rejects absolute paths, empty segments, `.`/`..`, NUL, backslash, malformed UTF-8, and overlong forms.
- `PrivateRoot` can only originate from an already-authorized directory handle.
- All descendant traversal is descriptor-relative, segment-by-segment, and `O_NOFOLLOW`.
- Final `File` objects must be regular files; FIFOs/sockets/devices/directories are rejected.
- `atomic_replace()` remains bounded same-directory temp/write/fsync/rename/parent-fsync.
- Do not add general hard-link or authority-expanding rename APIs casually.

## Sandbox/application boundary inherited by storage

- app executable is selected from a trusted immutable generation and launched from a retained object with `execveat(..., AT_EMPTY_PATH)`;
- fd inheritance is deny-by-default;
- current M1 application bootstrap still carries Linux-private fd 5 for the authorized private-data root;
- fd 5 is not public ABI and must be removed from application use during the next product-integration cutover;
- Landlock grants the private-data root write/create/remove rights but no execute authority;
- `no_new_privs`, cleared Linux capabilities, seccomp, bounded rlimits and parent-death policy remain layered beneath apps.

## Reference notes

Read `docs/REFERENCE_NOTES_2026_08_08.md` before architecture-sensitive work. The supplied source set reinforces process-granular trust, client/server ownership, object/handle APIs, data-caging separation and resource frugality from Symbian; stable higher-level OS services over private system-call mechanisms from the general OS texts; hardware-rooted update/rollback security direction from Knox; modern-crypto caution from NIST/BitLocker; upstream-first ARM64/Linux BSP work; hostile baseband/wireless input handling; continuously measured mobile performance; long-lived API discipline; and responsive/accessibility-first phone UX.

References are design evidence, not instructions to copy historical protocols, Android/Samsung vendor APIs, old crypto suites, Symbian ABI details, or educational from-scratch kernels.

## Next after M2.1

M2.2 is the product integration cutover.

Required direction:

- run Storage Service as a supervised system service with an explicit fd/memory/process budget;
- introduce a trusted system-only private-root provider/control path from installed application profile state into Storage Service;
- replace application use of the private-data directory bootstrap fd with a Storage Service connection or typed root capability;
- keep `PrincipalId + UserId` as private-data identity and process generation separate;
- add per-principal storage accounting and quota enforcement before storage behavior can become unbounded;
- define what outstanding object capabilities do when Storage Service restarts;
- keep Storage Service and future Key Service separate;
- document/media sharing comes later through brokers/object grants, not global filesystem visibility.

Do not jump to encryption, document sharing, media indexing, UI, telephony, or key-service work before the Storage Service integration/lifecycle boundary is settled.

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

For the focused M2.1 gate, build and run CTest tests matching `^storage_` on native x86-64 and native AArch64. The focused targets are `storage_private_storage_test`, `storage_service_integration_test`, and `storage_service_identity_test`.
