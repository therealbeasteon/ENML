# M0.8 Linux Sandbox Baseline

M0.8 introduces a real pre-exec restriction layer for supervised Linux services. It is intentionally an initial baseline rather than the final v1.3 sandbox backend.

## Enforced for supervised services

`os-supervisor` constructs bootstrap/service descriptors, closes unapproved inherited FDs, then calls `os::sandbox::apply_before_exec()` before `execve()`.

The baseline applies:

- parent-death `SIGKILL`
- umask `0077`
- `PR_SET_NO_NEW_PRIVS`
- zero effective/permitted/inheritable Linux capability sets
- `RLIMIT_CORE=0`
- bounded `RLIMIT_NOFILE`, `RLIMIT_NPROC`, and `RLIMIT_FSIZE`
- seccomp mode-filter with an M0 deny list for privilege/namespace/kernel-control operations
- fixed non-authoritative environment containing only `PATH` and `LANG`

The M0 seccomp deny list currently includes, where available on the architecture, operations such as `ptrace`, `mount`, `umount2`, `pivot_root`, `reboot`, kexec/module loading, swap control, `setns`, `unshare`, `bpf`, `perf_event_open`, and `open_by_handle_at`.

This is not the final syscall policy. A strict per-service allow-list is still planned after the actual syscall requirements of the core services are measured.

## Filesystem isolation

`core/ossandbox` contains an opt-in Landlock policy that can admit only the service executable, dynamic runtime library locations, the dynamic-loader cache, and read-only `/proc/self` metadata while denying other handled filesystem rights by omission.

The current execution container returns `ENOSYS` for Landlock syscalls even though its kernel headers/kernel version are recent. Therefore M0.8 does not silently require it. `sandbox_landlock_test` reports a CTest skip on such hosts and becomes a real write-denial test when Landlock is usable.

This means filesystem caging is **not yet a fully verified release gate**. M0.9 must retain this explicit sub-gate, and later sandbox work may use mount namespaces/LSM policy in addition to or instead of Landlock on target devices.

## Adversarial fixture

`evil_echo_service` is launched through the normal supervisor path. It refuses readiness unless it observes:

- `NoNewPrivs=1`
- seccomp filter mode
- empty Linux capability sets
- configured resource limits
- no inherited secret environment variable
- no unexpected descriptors above the bootstrap/service pair
- parent-death signal set to `SIGKILL`
- `unshare(CLONE_FILES)` denied with `EPERM` by the sandbox filter

Only after those checks does it accept the supervisor bootstrap and announce READY.

## Remaining sandbox work

M0.9 should expand escape probes, resource exhaustion, process-race testing, and filesystem isolation verification. Production work still requires semantic per-service policies, stronger filesystem/device isolation, cgroups/resource accounting, LSM integration, and the later Spawn Broker separation described by the architecture specification.
