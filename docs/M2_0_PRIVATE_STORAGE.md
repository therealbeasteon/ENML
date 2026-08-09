# M2.0 — Descriptor-Rooted Private Storage Foundation

M2.0 begins the storage/data-caging implementation track. It deliberately starts below the future Storage Service IPC surface: the first goal is to prove that private application storage can be expressed as typed, rooted objects without exposing a global Linux filesystem namespace to applications.

## Authority model

The authority root is an already-authorized per-application/per-user directory handle produced by trusted system policy.

```text
(ApplicationIdentity, UserId)
        ↓ trusted App Manager/profile state
private data directory object
        ↓
PrivateRoot
        ↓
Directory / File typed objects
```

`PrivateRoot::adopt_authorized_directory()` accepts a move-only `NativeHandle`, verifies that it names a directory, and internally reopens `.` as a close-on-exec directory object. It does not accept an absolute Linux pathname and does not expose its native descriptor.

M2.0 remains an internal runtime primitive. A later Storage Service milestone must place stable OSIDL/object-handle APIs in front of it and remove the current application bootstrap fd as an application-visible implementation detail.

## RelativePath

`RelativePath` is a fixed-capacity value type with no heap allocation. M2.0 defines:

- UTF-8 input;
- maximum path length: 1024 bytes;
- maximum segment length: 255 bytes;
- `/` as the only separator;
- no leading or trailing `/`;
- no empty segments;
- no `.` or `..` segments;
- no NUL bytes;
- no backslashes.

These rules intentionally keep the semantic namespace smaller than Linux pathname syntax. Unicode normalization is **not** performed in M2.0; security identity is based on the validated UTF-8 byte path beneath one trusted root. Display-name equivalence is a separate UI/filesystem policy question.

## Root confinement

Linux resolution is descriptor-relative and segment-by-segment:

```text
authorized dirfd
    ↓ openat(".")
segment 1: O_DIRECTORY | O_NOFOLLOW
    ↓
segment 2: O_DIRECTORY | O_NOFOLLOW
    ↓
parent dirfd
    ↓
final object: O_NOFOLLOW
```

Because `RelativePath` excludes `..`, absolute paths and empty components, and every intermediate component is opened as a real directory with `O_NOFOLLOW`, a symlink cannot redirect traversal outside the authorized root. Directory fds also preserve the resolved object if names are concurrently renamed.

The final file is opened nonblocking first and then checked with `fstat()`. Only regular files are accepted; FIFOs, sockets, devices and directories do not become `File` objects. This avoids a hostile special file turning a normal storage open into an unexpected blocking or device operation.

M2.0 intentionally exposes no hard-link creation API. Later storage-service policy must continue to avoid authority-expanding link operations.

## Typed objects and rights

`File` is move-only and contains a private `NativeHandle` plus the access rights derived from the trusted open operation.

Supported operations:

- `read_at(offset, MutableByteSpan)`
- `write_at(offset, ByteSpan)`
- `size()`
- `sync()`

Read and write methods check the typed object's recorded rights before issuing Linux I/O. Stable storage errors are returned instead of `errno`.

`Directory` is also move-only. It can produce narrower rooted `Directory` and `File` objects and perform rooted create/remove/atomic-replace operations. A child `Directory` therefore acts as a naturally reduced namespace authority.

This is not yet the final transferable OS object-rights system. M2.1+ should map these semantics to OSIP handle rights rather than serializing native descriptors or trusting an application-provided rights mask.

## Atomic replace

`atomic_replace(path, contents)` is a first-class bounded primitive because configuration/state files should not require applications to recreate a crash-consistency protocol themselves.

The M2.0 sequence is:

```text
resolve trusted parent directory
    ↓
validate existing target is absent or regular
    ↓
create same-directory .emnl-atomic-* temp with O_EXCL, mode 0600
    ↓
write bounded caller buffer (max 1 MiB)
    ↓
fsync(temp)
    ↓
renameat(temp → target)
    ↓
fsync(parent directory)
```

The temporary name is collision-resistant only by a process-local monotonic token; it is not a cryptographic secret and does not need to be. `O_EXCL` prevents accidental replacement and a bounded retry loop handles stale/colliding temp names. A later multi-process Storage Service can centralize or strengthen temporary-object management without changing the public atomic-replace semantic.

If the final parent-directory fsync fails after rename, the operation reports failure even though the namespace may already contain the new file. This is the normal durability ambiguity of a failure after an atomic namespace mutation and should remain explicit in higher-level transactional APIs.

## Error vocabulary

M2.0 introduces stable `ErrorDomain::storage` codes for invalid/escaping paths, missing/existing objects, directory/type mismatches, access denial, read-only filesystems, no-space/resource exhaustion, I/O failure, size/offset limits and invalid root/options.

Linux `errno` values remain private implementation details.

## Verification gate

`storage_private_storage_test` proves:

- valid bounded UTF-8 relative paths;
- rejection of absolute paths, trailing/double separators, `.`/`..`, backslashes, embedded NUL and malformed UTF-8;
- total/segment length bounds;
- non-directory root rejection;
- nested directory creation and typed subdirectory authority;
- regular file create/read/write/size/sync;
- read-only/write-only rights enforcement;
- missing-file error mapping;
- final symlink rejection;
- intermediate symlink escape rejection for both open and create;
- FIFO/special-file rejection without blocking;
- atomic create/replace and final mode 0600;
- atomic replacement refusal for a symlink target;
- rooted file/directory deletion;
- outside data remains unchanged.

The targeted M2 workflow runs this gate under GCC, Clang and native AArch64. The full existing M0/M1 CI also builds and tests the new library, including sanitizers and AArch64 cross/QEMU-safe tests.

## Deliberate limits

M2.0 does not yet provide:

- a Storage Service process or OSIDL interface;
- transferable file/directory object handles over OSIP;
- asynchronous I/O/task integration;
- document/media brokers;
- cross-application sharing;
- quotas/accounting;
- encryption or key management;
- backup/restore;
- recursive directory creation/removal;
- general rename/link APIs;
- filesystem watching/indexing.

Those features must preserve the root-confinement and typed-authority rules above.

## Next

M2.1 should put a narrow Storage Service/OSIDL boundary in front of this runtime and introduce transferable typed object handles with explicit read/write/stat/create rights. The service must derive the caller's private root from trusted `RequestContext` identity rather than accept a package ID, Linux path, uid, fd number, or PrincipalId claim from the request payload.
