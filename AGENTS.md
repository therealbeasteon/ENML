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
- M1.4 durable per-user application principals and private-data sandbox

Current branch milestone:

- M1.5 update/uninstall/revocation and generation retention

## M1.5 invariants

`PackageRegistry::uninstall()` removes only the active-generation selection. It retains signer-bound PackageId ownership and historical generation metadata. Reinstall/update under the same signer must continue monotonically; a different signer must still collide.

A live `ApplicationManager::InstanceSlot` is the authoritative generation pin. `retire_launch_target()` may release the retained executable object only when the generation is not active and no live instance uses it. Package Service may physically remove immutable code only after that succeeds.

`uninstall_application()` commits the durable no-active-generation state first, then revokes each running process from the supervisor identity registry before requesting termination. A slow-to-exit process therefore cannot keep normal supervisor-mediated service authorization merely because `waitpid()` has not completed.

Uninstall does not delete `ApplicationPrincipalStore` mappings or trusted per-user private-data profiles. Same-signer reinstall for the same user keeps the durable application PrincipalId/data profile but receives a fresh logical ProcessId and ApplicationInstanceId.

See `docs/M1_5_UPDATE_UNINSTALL.md` for the detailed lifecycle contract.

## M0/M1 sandbox baseline inherited by applications

- fixed environment (`PATH`, `LANG` only)
- deny-by-default descriptor inheritance
- `PR_SET_NO_NEW_PRIVS`
- empty effective/permitted/inheritable Linux capability sets
- seccomp filter denying privilege/namespace/kernel-control syscalls
- `RLIMIT_CORE=0`, bounded `RLIMIT_NOFILE`, `RLIMIT_NPROC`, and `RLIMIT_FSIZE`
- parent-death `SIGKILL`
- restrictive umask
- descriptor-rooted Landlock profile when required/supported
- exact executable launched from retained `O_PATH` object with `execveat(..., AT_EMPTY_PATH)`
- authorized per-user private-data root retained internally as fd 5 during current bootstrap

The final native AArch64 M0 gate verified Landlock. Keep explicit skip behavior on kernels/runtimes that cannot install the requested policy; do not weaken policy to turn a skip into a pass.

## Reference notes

Read `docs/REFERENCE_NOTES_2026_08_08.md` before security/mobile architecture work. The supplied source set reinforces:

- process-granular trust, capabilities, client/server resource ownership, data caging, OS-managed software installation, and resource-frugal phone design from Symbian material;
- hardware-rooted trust, defense-in-depth, rollback/tamper evidence, protected key material, and software-update lifecycle direction from Samsung Knox;
- AES as a standardized primitive, not permission to invent a custom encryption mode, from NIST FIPS 197-upd1;
- stable higher-level APIs over private kernel mechanisms from the operating-system texts;
- continuous CPU/memory/network/battery/responsiveness measurement for mobile performance;
- hostile wireless/protocol input handling and fuzzing for future Bluetooth/peripheral services.

Do not implement crypto, attestation, Bluetooth, or vendor-specific Knox mechanisms merely because references describe them; introduce them only in the milestone that owns that subsystem.

## Next after M1.5

Begin the storage/data-caging implementation track. Preserve all package/application lifecycle invariants and build stable typed storage APIs over the existing authorized per-app data root.

Priorities:

- private app storage must be rooted by trusted application identity/user context, never caller-supplied absolute Linux paths;
- introduce typed/move-only file and directory object handles with rights reduction;
- path traversal must be root-confined and reject symlink/reparse escapes;
- normal file API should use explicit bounded strings/spans/results and async-ready semantics without hiding unbounded worker pools;
- atomic replacement must be a first-class primitive;
- document/media access must later be brokered object authority, not global filesystem visibility;
- storage/key service separation must be preserved; do not invent encryption construction before the crypto/key milestone.

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
