# M1.4 — Durable Application Principals and Private-Data Sandbox

M1.4 removes the last provisional identity and sandbox choices from M1.3 launch-target registration. A normal application launch now resolves its principal and per-user private-data policy entirely from trusted persistent system state.

## Identity model

The persistent key is:

```text
(ApplicationIdentity { PackageId + SignerLineageId }, UserId)
```

The value is a device-local `PrincipalId` allocated by `ApplicationPrincipalStore`.

`PrincipalId` is an authorization identity, not a secret and not a cryptographic proof. M1.4 therefore uses a simple durable monotonic allocator rather than deriving identities from attacker-controlled text or truncating a hash. Allocated IDs are not reused by this store. The high 64 bits are an ENML application namespace tag and the low 64 bits are the persistent monotonic value.

A reinstall/update policy must not casually reset this allocator in later milestones. M1.5 uninstall/tombstone work owns those semantics.

## EPI1 persistent principal snapshot

The store uses an explicit bounded little-endian `EPI1` snapshot. Native C++ object layout is never persisted.

The snapshot records:

- format magic/version/header size/total size;
- record count;
- next monotonic principal value;
- canonical PackageId;
- exact SignerLineageId;
- UserId;
- allocated principal low half.

Loading rejects malformed/truncated/oversized state, duplicate `(ApplicationIdentity, UserId)` mappings, duplicate PrincipalIds, invalid signer/package identities, and a next-ID value that could reuse an already allocated ID.

Persistence follows the same crash-consistent pattern established for the package registry:

```text
candidate state
    ↓
encode bounded EPI1
    ↓
write .principals-v1.tmp mode 0600
    ↓
fsync(temp)
    ↓
renameat() → principals-v1.bin
    ↓
fsync(state directory)
    ↓
publish candidate in memory
```

The live database is opened with `O_NOFOLLOW`; a symlink cannot substitute for the principal database. Corrupt state is an error rather than an instruction to silently create a new identity universe.

## Trusted per-user application profile

`LaunchTargetRegistration` no longer contains a `PrincipalId` or `SandboxPolicyV1`. It only binds an exact trusted package generation to an already-authorized generation-directory handle plus the normalized manifest entry point.

Per-user runtime policy is registered separately as:

```text
ApplicationProfile =
    ApplicationIdentity
    + UserId
    + authorized private-data directory handle
    + SandboxPolicyV1
```

Applications cannot provide this profile. App Manager validates that the `ApplicationIdentity` is the signer-bound owner recorded by Package Service and retains an `O_PATH` descriptor to the exact private-data directory object.

A launch with no profile fails before a principal is allocated. Merely asking to launch an application therefore cannot manufacture durable identities for users who have no valid runtime policy.

## Descriptor-bound executable and data roots

The executable remains the exact object selected during trusted package registration. Path components are walked relative to the authorized package-generation directory with `O_NOFOLLOW`; the final object must be a regular executable file and is retained as `O_PATH`.

Immediately before application exec, the child owns only the deliberately placed bootstrap objects:

```text
fd 3  private application bootstrap channel
fd 4  exact immutable executable object (CLOEXEC)
fd 5  authorized per-user private-data root
```

Unexpected descriptors at 6 and above are closed before sandbox construction.

The executable is started with `execveat(fd4, "", ..., AT_EMPTY_PATH)`, so the launch decision is not re-resolved through an application-controlled pathname.

## Application Landlock profile

`apply_application_before_exec()` accepts already-authorized descriptors rather than public filesystem paths.

When the profile requires Landlock, M1.4 grants:

- the exact executable: read + execute;
- runtime library locations: read + execute;
- loader cache and narrow `/proc/self` material: read;
- the exact private-data root: read/write/create/remove rights, but **no execute right**.

All other handled filesystem rights are denied by omission. The private data handle is deliberately kept across exec as an internal runtime mechanism; public ENML storage APIs must wrap it rather than expose Linux fd numbers as stable application ABI.

The M0 protections remain layered underneath: `PR_SET_NO_NEW_PRIVS`, empty Linux capabilities, bounded rlimits, parent-death handling, fixed environment, and seccomp policy.

## Security invariants

M1.4 freezes these invariants for later milestones:

- app launch accepts `PackageId` plus trusted user context, never a Linux executable path;
- application code cannot choose PrincipalId, ProcessId, native credentials, active package generation, content digest, data root, or sandbox policy;
- the same signer-bound application and same user receive the same durable PrincipalId across launches and package generations;
- a different user receives a different application PrincipalId;
- a different signer lineage is a different application identity even if the textual PackageId matches;
- every process still receives a fresh logical ProcessId and every launch receives a fresh ApplicationInstanceId;
- activation of generation N+1 changes future launch resolution only; a running generation N instance keeps its generation/content identity;
- filesystem caging is descriptor-rooted and does not authorize an unrelated pre-opened directory merely because the process possesses its fd.

## Verification gates

The M1.4 unit/integration set covers:

- principal stability for repeated `(application,user)` resolution;
- user separation;
- signer-lineage separation;
- restart persistence and monotonic allocation;
- snapshot mode 0600;
- symlinked/corrupt principal state rejection;
- missing-profile launch rejection without principal allocation;
- generation-specific launch behavior from M1.3;
- stable application principal across generation activation;
- fresh process/instance identities per launch;
- internal private-data descriptor presence in the launched app;
- Landlock write success beneath the authorized data root;
- Landlock denial through an unrelated directory fd that was already open before restriction.

`application_sandbox_landlock_test` exits with CTest skip code 77 on a kernel/runtime that cannot install the required Landlock policy. Native ARM64 remains the strongest current Linux reference gate.

## Next: M1.5

M1.5 owns update/uninstall/revocation and generation-retention semantics. In particular it must define running-generation pins, prevent garbage collection of code still used by a live instance, block future launches promptly on uninstall, preserve non-reuse of application principals, and separate package-code removal from user-data/key destruction policy.
