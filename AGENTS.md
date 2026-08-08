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

M0.8 currently enforces, before service `execve`:

- fixed environment (`PATH`, `LANG` only)
- descriptor closure except bootstrap FD 3 and service FD 4
- `PR_SET_NO_NEW_PRIVS`
- empty effective/permitted/inheritable Linux capability sets
- seccomp filter denying privilege/namespace/kernel-control syscalls
- `RLIMIT_CORE=0`, bounded `RLIMIT_NOFILE`, `RLIMIT_NPROC`, and `RLIMIT_FSIZE`
- parent-death `SIGKILL`
- restrictive umask
- optional Landlock read-only runtime policy when the execution environment supports Landlock syscalls

Important: the current container returns `ENOSYS` for Landlock despite a recent kernel, so `sandbox_landlock_test` is a CTest skip here. Do not claim filesystem caging is fully verified until that test passes on a supporting host. Do not weaken the sandbox merely to avoid the skip.

## Next milestone: M0.10

Prove the complete M0 userspace substrate on ARM64 without changing the public EMNL abstractions. Priorities:

1. Add an AArch64 cross-build preset/toolchain without weakening host builds.
2. Cross-compile all M0 runtime libraries, `osidlc` outputs, `system.echo`, and `os-supervisor`.
3. Run the ARM64 userspace tests that are meaningful under QEMU user/system emulation.
4. Verify `WireHeaderV1` golden bytes are identical between x86-64 and AArch64.
5. Verify `sizeof`/native layout never enters the wire ABI.
6. Exercise `SOCK_SEQPACKET`, `SCM_RIGHTS`, and sender-credential behavior on the selected ARM64 Linux environment.
7. Re-run trusted identity, service restart, seccomp/resource, and malformed-input gates where the emulator/kernel supports them.
8. Keep unsupported host/kernel mechanisms explicit; do not silently convert skips into passes.
9. Record toolchain/QEMU/kernel versions and the exact commands used.
10. Do not start M1 until the M0.10 ARM64 gate is complete.

M0.9 evidence is in `docs/M0_9_CERTIFICATION.md`. The Landlock filesystem sub-gate remains explicitly unverified in the current container because the runtime returns `ENOSYS`.

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

Read `docs/M0_STATUS.md` before modifying architecture-sensitive code.
