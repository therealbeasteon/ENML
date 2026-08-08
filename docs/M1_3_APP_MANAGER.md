# M1.3 App Manager — Trusted Generation-Bound Launch

M1.3 adds the first real application launch path on top of the completed package registry and M0 supervisor identity substrate.

## Authority flow

A launch request supplies only a canonical `PackageId` plus the already-authenticated ENML user context. It does not supply a Linux path, Linux UID/GID, `PrincipalId`, content digest, package generation, or sandbox policy.

App Manager resolves:

1. `PackageId -> ApplicationIdentity` through the trusted package registry.
2. `ApplicationIdentity -> active PackageGenerationRecord` through durable package state.
3. The exact generation/content record -> a previously registered trusted launch target.
4. The application principal and sandbox from trusted launch-target state.
5. A fresh `ApplicationInstanceId` and supervisor-issued logical `ProcessId` for the new process.

A staged generation is not launchable merely because it exists. Only the active generation is selected by `launch()`.

## Trusted executable binding

Launch-target registration is an internal Package Service boundary, not an application API. It consumes:

- an exact `PackageGenerationRecord` already present in the trusted registry;
- a preallocated nonzero application `PrincipalId`;
- an already-authorized generation-directory handle;
- a normalized `ManifestPath` entry point from package analysis;
- a sandbox profile.

The executable is opened once during registration by walking each path segment relative to the generation directory with `O_NOFOLLOW`. Intermediate components must be directories and the final component must be a regular executable file. The resulting descriptor is retained by App Manager. Launch therefore does not re-resolve an application-controlled pathname.

M1.3 explicitly rejects launch targets requiring Landlock. Per-application package/data-root Landlock admission is M1.4 work; the current descriptor launch still receives the M0 `no_new_privs`, capability clearing, rlimits, and seccomp baseline.

## Process launch

For each instance, App Manager creates a private bootstrap channel and forks. The child places only the application bootstrap endpoint at FD 3 and the already-opened executable at FD 4. FD 4 is close-on-exec and is consumed with Linux `execveat(..., AT_EMPTY_PATH)`. Other inherited descriptors are closed before sandbox application and exec.

The parent registers the child with the supervisor-owned identity registry before sending bootstrap. The application receives only the resulting trusted `PeerIdentity`, its fresh `ApplicationInstanceId`, and the immutable package-generation number. It echoes the exact bootstrap record in its READY response before App Manager publishes the instance as launched.

## Generation semantics

Running instances snapshot their `ApplicationIdentity`, `PackageGenerationId`, content digest, principal, user, and logical process identity at launch. Activating generation N+1 changes only future launch resolution. It never mutates an already-running generation N instance.

One signer-bound `ApplicationIdentity` must use the same trusted principal for all registered generations. Principal allocation itself remains M1.4 policy; M1.3 accepts only a principal supplied through the trusted Package Service path and never from the application.

## M1.3 integration gate

The native integration fixture stages generations 1, 2, and 3, activates generation 1, and registers generation-specific executables. The generation-1 and generation-2 executables refuse READY unless the bootstrap generation matches the generation they were built for.

The test proves:

- symlink executable targets are rejected;
- a staged generation does not replace the active generation;
- two generation-1 launches receive distinct instance/process IDs;
- application principal/user identity comes from trusted state;
- activating generation 2 leaves existing generation-1 processes alive and bound to generation 1;
- a subsequent launch executes the generation-2 binary;
- cross-generation principal substitution is rejected;
- unknown packages cannot be launched;
- instance termination is reaped and its supervisor identity is revoked.

The test runs on native x86-64 and native AArch64. It is intentionally excluded from the QEMU-user cross gate because nested fork/exec of target AArch64 binaries is not a meaningful QEMU-user portability test.
