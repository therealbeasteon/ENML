# EMNL OS M0

Incremental implementation of the first EMNL OS userspace slice.

Current checkpoint:

- M0.0 build/repository foundation
- M0.1 `liboscore`
- M0.2 bounded `libosipc` wire codec
- M0.3 Linux `AF_UNIX` / `SOCK_SEQPACKET` channel transport
- M0.4 minimal `osidlc` compiler with generated Echo types/codecs/ABI metadata
- M0.5 typed cross-process Echo RPC with generated client/dispatcher bindings
- M0.6 `os-supervisor` lifecycle/bootstrap/readiness/restart vertical slice
- M0.7 trusted runtime identity (`PeerIdentity`, supervisor registry, pidfd-backed stale-PID defense)
- M0.8 initial Linux service sandbox (`no_new_privs`, empty capabilities, seccomp, bounded rlimits, fixed environment, adversarial probe)
- M0.9 adversarial/fault/resource certification gate (IPC handle flood, revocation/restart race, active rlimit probes, expanded seccomp escape matrix, RPC baseline, RPC error fuzzing)
- M0.10 ARM64 native + cross-build/QEMU validation infrastructure

M0.3 includes bounded packet receive, `SCM_RIGHTS` descriptor transfer, kernel-supplied per-message credentials via `SCM_CREDENTIALS`, connection credentials via `SO_PEERCRED`, strict ancillary validation, and peer-death behavior.

Kernel PID/UID/GID are transport evidence only. M0.7 now resolves those credentials through supervisor-published, pidfd-backed process records into `PeerIdentity { PrincipalId, UserId, ProcessId }` before generated service dispatch.

M0.4 adds the first deliberately small OSIDL language and build-time C++ generation. The current M0 struct encoding is named `appendable-positional-v0` and is explicitly pre-freeze.

M0.5 adds the first typed RPC path: generated clients encode requests through `libosipc`, `ClientConnection` assigns and validates RequestIds, generated dispatchers validate and decode requests, and typed responses/errors cross a real `SOCK_SEQPACKET` process boundary.

M0.6 adds a real `os-supervisor` and `system.echo` executable. The supervisor launches the service by direct `fork`/`exec`, passes a bounded bootstrap record over a private control channel, waits for explicit readiness, allocates a new logical ProcessId per generation, exposes reconnectable service endpoints, restarts on failure with bounded backoff, suppresses crash loops, and rejects never-ready children by timeout. No text manifest parser or shell execution exists in the supervisor.

## Build

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Sanitizers:

```sh
cmake --preset host-asan
cmake --build --preset host-asan
ctest --preset host-asan
```

Run the supervisor manually:

```sh
./build/host-debug/system/supervisor/os-supervisor \
  --echo-executable ./build/host-debug/system/services/echo/system.echo
```

Stop it with Ctrl-C or SIGTERM.

Generate Echo manually after a build:

```sh
./build/host-debug/tools/osidlc/osidlc \
  --input interfaces/echo/echo.osidl \
  --out-dir /tmp/emnl-echo-generated
```

M0.8 now adds a pre-exec Linux sandbox for supervised services: fixed environment, deny-by-default inherited descriptors, `PR_SET_NO_NEW_PRIVS`, empty effective/permitted/inheritable capability sets, bounded resource limits, parent-death kill behavior, and a seccomp filter for privilege/namespace/kernel-control syscalls. An `evil_echo_service` integration fixture verifies the sandbox before announcing readiness.

An opt-in Landlock filesystem policy is implemented, but the current execution container returns `ENOSYS` for Landlock syscalls. `sandbox_landlock_test` therefore reports a CTest skip here rather than silently pretending filesystem caging was verified. See `docs/M0_8_SANDBOX.md`.

M0.10 is the final M0 gate. Native AArch64 CI and the independent x86-64 → AArch64 cross-build must both pass before M0 is declared complete. See `docs/M0_10_ARM64.md`.
