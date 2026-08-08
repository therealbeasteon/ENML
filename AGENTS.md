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
- Public app storage APIs must not expose Linux fd numbers or Linux paths as stable application ABI even when the implementation uses descriptor-rooted caging internally.

## Current implementation status

Completed and merged:

- M0.0 repository/build
- M0.1 `liboscore`
- M0.2 bounded wire codec
- M0.3 Linux `SOCK_SEQPACKET` Channel + `SCM_RIGHTS` + `SCM_CREDENTIALS`
- M0.4 minimal `osidlc`
- M0.5 typed Echo RPC
- M0.6 `os-supervisor` lifecycle/readiness/restart
- M0.7 trusted `PeerIdentity` resolution with pidfd stale-PID defense
- M0.8 initial Linux service sandbox baseline
- M0.9 adversarial/fault/resource certification gate
- M0.10 ARM64 native/cross-build validation
- M1.0 package identity, signer continuity, immutable monotonic generations
- M1.1 bounded hostile package-manifest analyzer + fuzzing
- M1.2 durable package staging + atomic activation
- M1.3 trusted App Manager generation-bound launch

Current branch milestone:

- M1.4 durable per-user application principals and private-data sandbox

## M1.4 invariants

`ApplicationPrincipalStore` persistently maps `(ApplicationIdentity { PackageId + SignerLineageId }, UserId)` to a device-local `PrincipalId`. Allocation is monotonic and persisted in the explicit bounded little-endian `EPI1` snapshot. Principal IDs are authorization identifiers, not secrets; do not replace this with an application-supplied value or a truncated hash of package text.

App Manager launch-target registration now binds only trusted package generation + authorized generation-directory handle + normalized manifest entry point. `PrincipalId` and `SandboxPolicyV1` are no longer launch-target inputs.

A separate trusted per-user application profile binds `ApplicationIdentity + UserId` to an authorized private-data directory handle and sandbox policy. Launch fails before principal allocation if no profile exists.

The child is constructed with Linux-private bootstrap descriptors:

- fd 3: application bootstrap channel
- fd 4: exact immutable executable object, close-on-exec
- fd 5: authorized per-user private-data root

Everything at fd 6 and above is closed before sandbox construction. The executable is an `O_PATH` object selected with segment-by-segment `O_NOFOLLOW` resolution and is started by `execveat(..., AT_EMPTY_PATH)`.

When required, the application Landlock profile grants execute/read only to the exact executable/runtime and grants read/write/create/remove rights beneath the exact private-data root without execute permission. Possession of an unrelated pre-opened directory fd must not bypass this caging.

See `docs/M1_4_PRINCIPALS_SANDBOX.md` for the detailed milestone contract.

## M0 sandbox baseline inherited by applications

- fixed environment (`PATH`, `LANG` only)
- deny-by-default descriptor inheritance
- `PR_SET_NO_NEW_PRIVS`
- empty effective/permitted/inheritable Linux capability sets
- seccomp filter denying privilege/namespace/kernel-control syscalls
- `RLIMIT_CORE=0`, bounded `RLIMIT_NOFILE`, `RLIMIT_NPROC`, and `RLIMIT_FSIZE`
- parent-death `SIGKILL`
- restrictive umask

The final native AArch64 M0 gate verified Landlock. Keep explicit skip behavior on kernels/runtimes that cannot install the requested policy; do not weaken policy to turn a skip into a pass.

## Reference notes

Read `docs/REFERENCE_NOTES_2026_08_08.md` before security/mobile architecture work. The supplied source set reinforces:

- process-granular trust, capabilities, client/server resource ownership, data caging, and resource-frugal phone design from Symbian material;
- hardware-rooted trust, defense-in-depth, rollback/tamper evidence, protected key material, and attestation direction from Samsung Knox;
- AES as a standardized primitive, not permission to invent a custom encryption mode, from NIST FIPS 197-upd1;
- stable higher-level APIs over private kernel mechanisms from the operating-system texts;
- continuous CPU/memory/network/battery/responsiveness measurement for mobile performance;
- hostile wireless/protocol input handling and fuzzing for future Bluetooth/peripheral services.

Do not implement crypto, attestation, Bluetooth, or vendor-specific Knox mechanisms merely because references describe them; introduce them only in the milestone that owns that subsystem.

## Next after M1.4

M1.5 should implement update/uninstall/revocation and generation-retention semantics while preserving M1.3/M1.4 invariants. Priorities:

- a live application instance pins the exact immutable generation it runs;
- active-generation changes affect future launch only;
- garbage collection cannot remove a generation while a live instance pins it;
- uninstall blocks future launches and new background registrations promptly;
- application principal values are not recycled as an uninstall side effect;
- package-code removal is separate from private user-data deletion and later cryptographic-key destruction;
- same textual PackageId under a different signer lineage must never inherit old identity/data accidentally;
- no arbitrary install/uninstall scripts.

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

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, the current milestone document, and the reference notes before modifying architecture-sensitive code.
