# M2 Status

## M2.0 — private storage foundation

Status: complete and merged.

Implemented:

- bounded fixed-capacity UTF-8 `RelativePath`
- relative-path rejection for absolute, empty, dot/dot-dot, NUL, backslash and malformed UTF-8 forms
- stable storage-domain errors with no public errno leakage
- move-only typed `File` and `Directory` objects
- trusted `PrivateRoot` created only from an authorized directory handle
- segment-by-segment descriptor-relative `O_NOFOLLOW` traversal
- regular-file-only open semantics
- typed read/write access enforcement
- positional read/write, size and sync
- rooted child-directory creation/open/removal
- rooted regular-file removal
- bounded same-directory atomic replacement with file+parent fsync
- symlink/intermediate escape and special-file adversarial tests
- GCC, Clang and native AArch64 targeted storage CI
- full inherited M0/M1 regression coverage

See `docs/M2_0_PRIVATE_STORAGE.md`.

## M2.1 — Storage Service + typed object capabilities

Current branch: `m2-1-storage-service-handles`

Implemented:

- Storage Service OSIDL contract with a distinct stable service id
- trusted private-root selection from `RequestContext.peer`, keyed by durable `PrincipalId + UserId`
- no caller-supplied package id, principal id, uid/gid, fd number or absolute path in root selection
- successful RPC responses can transfer bounded handles through `SCM_RIGHTS`
- move-only `DirectoryObjectHandle` and `FileObjectHandle` wrappers
- dedicated object endpoints rather than serialized native descriptors
- server-authoritative file and directory rights masks
- monotonic child-directory rights reduction
- raw rights-escalation rejection even when typed client prechecks are bypassed
- bounded synchronous file I/O and atomic replacement over the existing 64 KiB OSIP transport
- fixed-capacity root policy and live-object tables
- object endpoint cleanup on peer death/hangup
- inherited main-channel identity attack test using per-message `SCM_CREDENTIALS`
- focused GCC, Clang and native AArch64 storage gates

See `docs/M2_1_STORAGE_SERVICE.md`.

## Next

M2.2: product integration cutover. Run Storage Service as a supervised system service, introduce the trusted profile/root-provider control path, replace application use of the Linux-private fd-5 data root with a Storage Service connection/object capability, and add per-principal storage accounting/quota enforcement. Outstanding object-handle behavior across service restart must be explicit rather than accidental.
