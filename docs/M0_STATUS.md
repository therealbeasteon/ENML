# M0 Status

## Completed

- M0.0 repository/build foundation
- M0.1 `liboscore`
- M0.2 bounded `libosipc` wire codec
- M0.3 Linux local channel transport
- M0.4 minimal `osidlc` compiler and generated Echo schema/codecs
- M0.5 typed Echo cross-process RPC and generated dispatch/invocation
- M0.6 `os-supervisor` process lifecycle, bootstrap/readiness, restart, crash-loop suppression, and reconnectable Echo service
- M0.7 supervisor-owned runtime identity registry, pidfd-backed stale-process defense, PrincipalId/UserId resolution, and forged-identity rejection
- M0.8 initial Linux service sandbox with fixed exec environment, `no_new_privs`, cleared Linux capabilities, seccomp, bounded rlimits, parent-death handling, and adversarial probe service
- M0.9 adversarial/fault/resource certification with IPC handle/payload stress, descriptor-leak checks, restart/revocation race coverage, active RLIMIT enforcement probes, expanded seccomp escape attempts, RPC baseline measurement, and RPC error fuzzing

## M0.2 invariants

- Explicit little-endian serialization; no native C++ struct serialization.
- `WireHeaderV1` is a 40-byte logical wire format with `OSIP` magic.
- Transport version is 1.
- Inline payload is bounded to 64 KiB.
- Handle count is bounded to 16.
- Exactly one primary message class is required: REQUEST, RESPONSE, or EVENT.
- ERROR is only valid on RESPONSE.
- ONEWAY and CANCELLABLE are request modifiers and cannot be combined with each other in v1.
- HAS_HANDLES must exactly agree with `handle_count > 0`.
- Reserved header bits/fields are rejected when nonzero/unknown.
- Boolean wire representation is canonical 0 or 1.
- Byte strings and UTF-8 strings are u32-length-prefixed and caller-bounded.
- UTF-8 rejects overlong encodings, surrogate code points, invalid continuation bytes, and values above U+10FFFF.
- `payload_checksum` is reserved as zero in transport v1; it is not an authentication mechanism.
- Fixed header encoding performs zero dynamic allocations.

## M0.3 invariants

- Linux reference transport uses `AF_UNIX` + `SOCK_SEQPACKET` and preserves message boundaries.
- Channel send validates wire payload/handle counts before entering the kernel.
- Receive uses a fixed maximum packet buffer and rejects truncated/oversized packets.
- Descriptor passing uses `SCM_RIGHTS`, is limited to 16 descriptors, and received descriptors are `FD_CLOEXEC`.
- `SO_PASSCRED` + `SCM_CREDENTIALS` captures kernel-supplied sender PID/UID/GID per packet.
- `SO_PEERCRED` is exposed only as connection-level transport evidence.
- Linux PID/UID/GID are explicitly not `PrincipalId`; supervisor/runtime mapping remains required before authorization.
- Ancillary data is strict: unknown/malformed control records and control truncation are rejected.
- Received descriptor count must exactly match the wire header.
- A message must contain exactly the declared payload bytes; trailing/truncated payload is rejected.
- Peer closure maps to the stable IPC `peer_died` error.
- Returned payload views borrow the caller-owned receive buffer.

## M0.4 invariants

- `osidlc` is a native build tool with a deterministic lexer/parser/semantic pass; it does not use reflection or a runtime schema parser in services.
- The M0 language intentionally supports only `package`, `struct`, `service`, `version`, `operation`, `u32`, `u64`, and bounded `string<N>`.
- The first checked-in interface is `interfaces/echo/echo.osidl` with service ID `0x0000F001`, Ping operation 1, and Identify operation 2.
- Service IDs, operation IDs, field IDs, string bounds, referenced request/response types, duplicate names/IDs, and maximum encoded sizes are validated at build time.
- Fields must use strictly increasing numeric IDs in M0 so declaration order is append-only wire order.
- Generated outputs are deterministic and include a diagnostic source hash, compiler version, and canonical ABI JSON.
- Generated C++ types are plain value/view types and the generated codecs use `libosipc::Encoder`/`Decoder` rather than native C++ object layout.
- M0.4 uses an explicitly experimental `appendable-positional-v0` struct envelope: a u32 body length followed by fields in ascending ID order.
- An older decoder can ignore unknown appended trailing fields inside the struct envelope. A newer decoder defaults missing trailing fields to zero/empty. This is an M0 compatibility experiment, not yet the externally frozen application wire schema.
- Strings remain bounded UTF-8 and decode as borrowed `std::string_view` values into the caller-owned message buffer.
- M0.4's struct schema remains experimental/pre-freeze while the RPC layer provides implementation evidence.

## M0.5 invariants

- `ClientConnection` assigns nonzero connection-local request IDs and validates that each response matches service ID, operation ID, and request ID.
- RPC is synchronous and single-flight in M0.5; asynchronous/multiplexed calls are deferred until `Task<T>` and structured concurrency are implemented.
- RESPONSE and RESPONSE|ERROR are distinct protocol states; ERROR payloads use a fixed canonical 8-byte `ErrorDomain/reserved/code` envelope.
- Malformed remote error envelopes are rejected as IPC protocol violations.
- Generated clients encode typed requests, call through `ClientConnection`, and decode typed responses without exposing wire construction to service consumers.
- Generated dispatchers validate request class/service identity before decoding and dispatch only known operation IDs.
- Unknown operations return a stable service-domain `unknown_operation` error; malformed typed request payloads return their bounded decoder error and the dispatcher remains usable.
- `RequestContext` no longer exposes raw kernel credentials to generated service implementations. M0.7 resolves them through a trusted identity resolver into `PeerIdentity { PrincipalId, UserId, ProcessId }`.
- Generated response string views borrow the caller-provided receive packet buffer. The caller must keep that buffer alive and unchanged for the view lifetime.
- A server process can die after valid RPCs; subsequent client calls map cleanly to `peer_died`.
- A normal generated Echo Ping performs zero C++ dynamic allocations on the measured client call path.

## M0.6 invariants

- `os-supervisor` exists as a real executable and a small reusable supervisor library.
- The M0.6 service description is a compiled `ServiceDescriptorV1`; PID 1 does not parse YAML/JSON/XML or invoke a shell.
- Service launch uses direct `fork`/`exec` with fixed bootstrap FD 3 and service endpoint FD 4.
- Bootstrap/control and service data use separate `SOCK_SEQPACKET` channels.
- Child descriptor inheritance is constrained before exec; bootstrap/service descriptors are deliberately placed and other inherited descriptors are closed.
- Supervisor assigns a new logical `ProcessId` before every service generation. Linux PID remains private transport/process evidence.
- The supervisor sends an explicit bounded bootstrap record. M0.7 extends it with the service `PeerIdentity { PrincipalId, UserId, ProcessId }`; argv/environment remain non-authoritative.
- A successful `exec` is not readiness. `system.echo` must validate bootstrap, initialize its dispatcher, and send a readiness response before state becomes RUNNING.
- Readiness has a bounded timeout; a process that never marks ready is killed and never published as RUNNING.
- Clients connect by duplicating the supervisor-owned current service endpoint. A service restart creates a fresh endpoint; old connections fail with `peer_died` and new connections reach the new generation.
- Child exit/reaping remains owned by `waitpid` against the exact managed child, while M0.7 additionally keeps pidfds for identity freshness and stale-PID defense. Public `ProcessId` remains separate from both mechanisms.
- On-failure restart uses a bounded delay and a restart budget/window. Exceeding the budget transitions the service to CRASH_LOOP instead of spinning forever.
- Normal service exit does not trigger on-failure restart.
- `os-supervisor` handles SIGTERM/SIGINT and shuts its managed child down through supervisor destruction.
- M0.6 is intentionally a single-service vertical slice. Dependency DAGs, multi-service directory policy, principals, cgroups, namespaces, seccomp, and production sandboxing remain later M0/M1 work.

## M0.7 invariants

- `PrincipalId`, `UserId`, and logical `ProcessId` are distinct from Linux PID/UID/GID and are represented by `PeerIdentity`.
- The supervisor owns the authoritative process registry and allocates boot-scoped logical `ProcessId` values.
- Service bootstrap now carries the service's own `PeerIdentity`; argv/environment remain non-authoritative.
- External process registration is explicitly supervisor-mediated. Merely possessing a service channel does not establish an EMNL identity.
- The supervisor opens a Linux pidfd for every registered process and retains it with the mapping. Services receive a duplicated pidfd over the private control channel.
- Service-side `IdentityRegistry` requires exact PID/UID/GID match and a live pidfd before resolving a request. Dead-process entries are rejected/removed, preventing stale native PID reuse from resurrecting an old identity.
- Generated dispatchers require a `PeerIdentityResolver`; `validate_rpc_request` resolves kernel `SCM_CREDENTIALS` before constructing `RequestContext`.
- `RequestContext` contains `PeerIdentity` plus `RequestId`; service code does not authorize against raw Linux credentials.
- Identity registration/unregistration uses the private supervisor/service control channel and stable request/response error handling.
- A service restart creates a fresh service ProcessId and in-process identity registry. The supervisor republishes still-live external identities before declaring the new service generation RUNNING.
- A channel inherited/passed to an unregistered process does not transfer the registered sender's identity because per-packet `SCM_CREDENTIALS` exposes the actual sender PID.
- The Echo test service includes a deliberate `IdentifyClaim` adversarial operation. Claimed ProcessId/PrincipalId/UserId fields are ignored; the response always reflects trusted `RequestContext.peer`.
- Explicit process unregistration revokes future service authorization even while the native process remains alive.
- `pidfd_open` is an M0.7 Linux requirement for the reference runtime identity implementation.


## M0.8 invariants

- Every supervised service passes through `os::sandbox::apply_before_exec()` after descriptor placement/closure and before `execve()`.
- The service environment is fixed to `PATH` and `LANG`; inherited environment variables are not authority and do not cross the exec boundary.
- `PR_SET_NO_NEW_PRIVS` is required for the default M0.8 profile.
- Effective, permitted, and inheritable Linux capability sets are cleared before exec; `no_new_privs` prevents file-exec privilege gain.
- `RLIMIT_CORE` is zero and `RLIMIT_NOFILE`, `RLIMIT_NPROC`, and `RLIMIT_FSIZE` are bounded by `SandboxPolicyV1`.
- Services receive a parent-death `SIGKILL` policy and restrictive umask before exec.
- M0.8 seccomp is an initial deny-list baseline, not the final per-service allow-list. It rejects privilege/namespace/kernel-control syscalls including `unshare` on supported architectures.
- The adversarial `evil_echo_service` refuses READY unless it observes no-new-privs, seccomp filter mode, empty capabilities, expected rlimits, fixed environment, no unexpected inherited descriptors, parent-death behavior, and an `EPERM` result for an otherwise-unprivileged `unshare(CLONE_FILES)` attempt.
- An opt-in Landlock policy exists for initial filesystem restriction. The current execution environment returns `ENOSYS` for Landlock, so `sandbox_landlock_test` is explicitly skipped here. Filesystem caging is therefore not yet a fully verified release gate and must remain visible in M0.9.

## M0.9 invariants

- `channel_stress_test` drives hundreds of cross-process packets, including repeated maximum-size 64 KiB payloads and repeated 16-descriptor `SCM_RIGHTS` transfers. Receiver descriptor counts return to baseline after every message, demonstrating bounded handle ownership/reclamation rather than cumulative leakage.
- Received transferred descriptors remain `FD_CLOEXEC` under repeated handle-flood conditions.
- Supervisor revocation is tested during the restart window: if an external identity is revoked after service death but before the new generation reaches RUNNING, the replacement service must not receive that identity during republish.
- The resource probe verifies `RLIMIT_NOFILE` by opening descriptors until the kernel returns `EMFILE`, and verifies `RLIMIT_FSIZE` by writing a regular file until the kernel returns `EFBIG` at the configured boundary. `RLIMIT_CORE=0` is also rechecked.
- The sandbox escape fixture now checks multiple seccomp interception paths. `unshare(CLONE_FILES)` and invalid-argument `setns`/`ptrace`/`open_by_handle_at` probes must return `EPERM` from the filter before their normal syscall-specific errors.
- The generated Echo RPC baseline executes 2,000 typed round trips across a real process boundary and prints timing evidence. The local GCC Debug measurement for this checkpoint was 53,740 microseconds total (26.87 microseconds/call); this is a baseline observation, not a frozen product SLA.
- A dedicated `rpc_error_fuzz` target now fuzzes the canonical remote-error envelope under libFuzzer + ASan + UBSan.
- Security bounds are not relaxed for performance or stress testing: 64 KiB inline payload and 16 transferred handles remain the M0 limits.
- The Landlock filesystem-isolation sub-gate remains explicit. This runtime returns `ENOSYS`, so the capability test is skipped rather than counted as a pass.

## Verification checkpoint

- Host GCC Debug: 30 passing tests + 1 Landlock capability test skipped by runtime.
- Host GCC ASan/UBSan: 30 passing tests + 1 Landlock capability test skipped by runtime.
- Host Clang Debug: 30 passing tests + 1 Landlock capability test skipped by runtime.
- Clang/libFuzzer IPC decoder smoke run: 10,000 executions clean.
- Clang/libFuzzer `osidlc` compiler smoke run: 10,000 executions clean.
- Clang/libFuzzer RPC error-envelope smoke run: 10,000 executions clean.
- Cross-process IPC stress: 384 packets, 12 maximum 64 KiB payloads, and 48 packets carrying the maximum 16 descriptors; receiver FD count returns to baseline after each message.
- Resource enforcement: active `EMFILE` and `EFBIG` kernel failures observed at the configured sandbox limits.
- Restart/revocation race: identity revoked while service is in `RESTART_WAIT` remains unauthorized after the replacement service reaches RUNNING.
- RPC timing observation: 2,000 typed Ping round trips in 53,740 microseconds total on this container's GCC Debug build (26.87 microseconds/call).
- Existing lifecycle, crash-loop, readiness, forged-identity, stale-pidfd, malformed IPC/RPC, zero-allocation, and sandbox probes remain green.

## Next

M0.10: ARM64 cross-build and QEMU validation. Preserve identical wire bytes and service semantics, exercise Linux transport/security mechanisms on AArch64 where available, record unsupported emulator/kernel sub-gates explicitly, and do not begin M1 until this gate is complete.
