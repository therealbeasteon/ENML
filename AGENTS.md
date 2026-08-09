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
- `WireHeaderV1` is a 40-byte logical format. Inline payload remains bounded to 64 KiB and transferred handles to 16 during M0.
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

## Completed implementation

M0.0-M0.10 are complete: build/oscore, bounded OSIP codec/Channel, OSIDL, typed Echo, supervisor lifecycle, trusted identity, Linux sandbox, adversarial/resource gate, and ARM64 native/cross validation.

M1.0-M1.5 are complete and merged: signer-bound package identity, hostile manifest analysis, durable staging/activation, trusted generation-bound App Manager launch, durable per-user app principals/private-data sandbox, and update/uninstall/revocation/generation-retention semantics.

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, and `docs/M1_5_UPDATE_UNINSTALL.md` before changing those substrates.

## Current branch milestone: M2.0 private storage foundation

M2.0 introduces an internal storage runtime beneath the future Storage Service.

`RelativePath` is a fixed-capacity UTF-8 byte path. It rejects absolute paths, leading/trailing/double separators, `.`/`..`, embedded NUL, backslash, malformed UTF-8, overlong total paths and overlong segments. Do not loosen these rules to mirror Linux pathname syntax.

`PrivateRoot` is created only from an already-authorized directory handle. It never accepts a Linux path and never exposes its native descriptor. `Directory` and `File` are move-only typed objects.

All descendant resolution is descriptor-relative and segment-by-segment with `O_NOFOLLOW`. Intermediate objects must be real directories. Final `File` objects must be regular files; FIFOs/sockets/devices/directories are rejected. A child `Directory` is a naturally reduced namespace authority.

`File` supports bounded positional read/write, size and sync with stable `ErrorDomain::storage` errors. Access rights are recorded at open and checked before I/O; never trust a later caller-supplied rights mask.

`atomic_replace()` is a bounded same-directory temp/write/fsync/rename/parent-fsync primitive. The payload is borrowed and capped at 1 MiB for M2.0. Temporary names are not secrets; `O_EXCL` and bounded collision handling are the safety mechanism.

See `docs/M2_0_PRIVATE_STORAGE.md` and `docs/M2_STATUS.md`.

## Sandbox/application boundary inherited by storage

- app executable is selected from a trusted immutable generation and launched from a retained object with `execveat(..., AT_EMPTY_PATH)`;
- fd inheritance is deny-by-default;
- current internal app bootstrap uses fd 5 for the authorized private-data root;
- Landlock grants the private-data root write/create/remove rights but no execute authority;
- `no_new_privs`, cleared Linux capabilities, seccomp, bounded rlimits and parent-death policy remain layered beneath apps.

M2.1 should replace direct bootstrap-root use with a real Storage Service/object-handle API. Do not make fd 5 part of public ABI.

## Reference notes

Read `docs/REFERENCE_NOTES_2026_08_08.md` before architecture-sensitive work. The source set reinforces process-granular trust/data caging and resource frugality (Symbian), hardware-rooted update/rollback security direction (Knox), modern-crypto caution (NIST/BitLocker), private Linux kernel mechanisms with upstream-first BSP work, hostile baseband/wireless inputs, continuously measured mobile performance, long-lived API discipline, responsive/accessibility-first UI design, and explicit threat modeling for duress/security UX.

References are design evidence, not instructions to copy historical protocols, Android/Samsung vendor APIs, old crypto suites, Symbian ABI details, or educational from-scratch kernels.

## Next after M2.0

M2.1 should put a narrow Storage Service/OSIDL boundary in front of `osstorage` and introduce transferable typed object handles with explicit rights reduction.

Key requirements:

- derive the caller's private root from trusted `RequestContext` identity, never from caller-supplied PackageId/PrincipalId/uid/path/fd;
- return typed file/directory object handles, not native fd values;
- rights delegation may only reduce rights;
- keep path strings bounded and relative to an already-authorized directory object;
- preserve atomic replace as a semantic operation;
- add quotas/accounting before unbounded app storage behavior;
- keep Storage Service and future Key Service separate;
- document/media sharing comes later through brokers/object grants, not global filesystem visibility.

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

For the focused M2.0 gate, run CTest tests matching `^storage_` on native x86-64 and native AArch64.
