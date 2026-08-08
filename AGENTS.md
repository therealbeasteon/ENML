# EMNL OS — Codex Working Instructions

This repository is an incremental implementation of EMNL OS. Continue the existing architecture; do not redesign it into Android, a conventional desktop Linux distribution, a monolithic daemon, or a from-scratch kernel.

## Product direction

EMNL aims for appliance-like phone behavior, small trusted components, strong process isolation, bounded IPC, stable public APIs, low idle work, fast boot/resume, and a modern phone UX. Linux is the private hardware/process kernel. EMNL services, OSIDL, security identities, package/storage/UI/telephony APIs are the public OS personality.

## Hard architectural constraints

- ARM64 is the primary target; host x86-64 Linux is the current implementation tier.
- Linux PID/UID/GID are implementation evidence, never public EMNL identity.
- `PrincipalId`, `UserId`, and logical `ProcessId` are resolved by the supervisor-owned identity registry.
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
- Application launch is package-based, never arbitrary-path-based. Apps must not choose Linux executable paths, native credentials, `PrincipalId`, active generation, content digest, or sandbox policy.
- A staged package generation is not active merely because it exists. Activation affects future launches only; a running process stays bound to the generation that created it.

## Current implementation status

Completed:

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

Current branch milestone:

- M1.3 App Manager trusted generation-bound launch

M1.3 resolves `PackageId -> ApplicationIdentity -> active PackageGenerationRecord`, then selects an internal Package Service-registered launch target. The executable is opened during trusted registration relative to an authorized generation-directory handle with `O_NOFOLLOW`, retained as a descriptor, and launched with `execveat(..., AT_EMPTY_PATH)`. The application receives a fresh `ApplicationInstanceId` and supervisor-issued `PeerIdentity` over a bounded bootstrap protocol before READY. One signer-bound application must retain one trusted principal across registered generations. Principal allocation/persistence itself remains M1.4.

M1.3 deliberately rejects launch targets requiring Landlock because per-application package/data-root admission is not yet defined for descriptor launch. Do not weaken this into pathname launch. M1.4 should add per-app roots/policy while preserving descriptor-based executable binding.

## M0 sandbox baseline inherited by M1.3 apps

- fixed environment (`PATH`, `LANG` only)
- deny-by-default descriptor inheritance
- `PR_SET_NO_NEW_PRIVS`
- empty effective/permitted/inheritable Linux capability sets
- seccomp filter denying privilege/namespace/kernel-control syscalls
- `RLIMIT_CORE=0`, bounded `RLIMIT_NOFILE`, `RLIMIT_NPROC`, and `RLIMIT_FSIZE`
- parent-death `SIGKILL`
- restrictive umask

The final native AArch64 M0 gate verified the Landlock test. Do not weaken sandbox policy to make a particular host pass.

## New reference notes

Read `docs/REFERENCE_NOTES_2026_08_08.md` before security/mobile architecture work. The new source set reinforces:

- process-granular trust, capabilities and data caging from Symbian material;
- hardware-rooted trust, defense-in-depth and attestation direction from Samsung Knox;
- AES as a standardized primitive, not permission to invent a custom encryption mode, from NIST FIPS 197-upd1;
- continuous CPU/memory/network/battery/responsiveness measurement for mobile performance;
- hostile wireless/protocol input handling and fuzzing for future Bluetooth/peripheral services.

Do not implement crypto, attestation, or Bluetooth merely because the references describe them; introduce them only in the milestone that owns that subsystem.

## Next after M1.3

M1.4 should add trusted per-user application-principal allocation/persistence and per-application sandbox/data-root construction. Preserve all M1.3 generation and launch invariants. Then M1.5 can finish update/uninstall/revocation/generation-retention semantics.

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

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, and the current milestone document before modifying architecture-sensitive code.
