# M0.9 Adversarial / Fault / Resource Certification

M0.9 does not add a new public OS feature. It hardens the existing M0 substrate by turning previously stated failure/security properties into executable gates.

## IPC stress and handle ownership

`channel_stress_test` uses a real forked process and `SOCK_SEQPACKET` channel. It sends 384 packets, including 12 maximum-size 64 KiB inline payloads and 48 messages carrying the maximum 16 `SCM_RIGHTS` descriptors. The receiver verifies every received descriptor is `FD_CLOEXEC` and checks `/proc/self/fd` after every message scope to ensure descriptor ownership returns to its baseline rather than leaking over the flood.

## Restart / revocation race

`supervisor_restart_revocation_test` kills `system.echo`, drives the supervisor into `RESTART_WAIT`, revokes the external client's logical ProcessId while no service generation is running, then waits for the replacement service. The new generation rejects the client as `security::unknown_process`, proving restart republish cannot resurrect an identity revoked during the gap.

## Active resource enforcement

`resource_probe_service` is launched through the ordinary supervisor sandbox with `RLIMIT_NOFILE=12`, `RLIMIT_FSIZE=4096`, and `RLIMIT_CORE=0`. It does not merely read the configured values: it opens descriptors until the kernel returns `EMFILE`, and writes a temporary regular file until the kernel returns `EFBIG` exactly at the configured file-size boundary. Only then does it announce READY.

## Expanded syscall escape probes

The adversarial sandbox service retains the original `unshare(CLONE_FILES)` test and adds invalid-argument probes for `setns`, `ptrace`, and `open_by_handle_at` where those syscalls exist. Each must return `EPERM`, demonstrating seccomp interception before the normal syscall-specific error path. These remain a baseline deny-list proof, not the final per-service syscall allow-list.

## RPC parser fuzzing

`rpc_error_fuzz` feeds arbitrary byte strings to the canonical remote-error envelope decoder under libFuzzer + ASan + UBSan. The M0.9 checkpoint completed 10,000 clean executions in addition to the existing IPC-header and OSIDL compiler fuzz smoke runs.

## Performance evidence

`echo_rpc_baseline_test` performs 2,000 generated typed Ping round trips across a real process boundary. On the checkpoint container's GCC Debug build it measured 53,740 microseconds total, or 26.87 microseconds per call. This is evidence for regression comparison only; it is not a frozen latency SLA and must not be used to justify weakening bounds or security checks.

## Explicit incomplete sub-gate

Filesystem caging through the opt-in Landlock implementation remains unverified in this execution container because the Landlock syscalls return `ENOSYS`. `sandbox_landlock_test` therefore remains a CTest skip. M0.10 and later real-board work must keep this limitation visible rather than treating it as a pass.
