# M2 Status

## M2.0 — private storage foundation

Status: complete and merged.

Implemented:

- bounded fixed-capacity UTF-8 `RelativePath`
- rejection of absolute, empty, dot/dot-dot, NUL, backslash, malformed UTF-8 and overlong paths
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

See `docs/M2_0_PRIVATE_STORAGE.md`.

## M2.1 — Storage Service + typed object capabilities

Status: complete and merged.

Implemented:

- distinct stable Storage and storage-object service ids
- private-root selection from trusted `RequestContext.peer`, keyed by durable `PrincipalId + UserId`
- no caller-supplied package id, principal id, uid/gid, fd number or absolute path in public root selection
- bounded successful RPC handle transfer through `SCM_RIGHTS`
- move-only `DirectoryObjectHandle` and `FileObjectHandle`
- dedicated object endpoints rather than serialized native descriptors
- server-authoritative file/directory rights masks and monotonic rights reduction
- raw rights-escalation rejection when typed client checks are bypassed
- bounded synchronous file I/O and atomic replacement over OSIP
- fixed-capacity root and live-object tables
- object cleanup on peer death/hangup
- inherited-channel identity attack coverage through per-message `SCM_CREDENTIALS`

See `docs/M2_1_STORAGE_SERVICE.md`.

## M2.2 — Storage product integration

Status: implementation/review gate in progress on `m2-2-storage-product-integration`.

Implemented in the current branch:

- real supervised `system.storage` executable
- trusted root publication over the private supervisor control channel
- App Manager retains the durable profile identity/root and republishes it when the Storage service generation changes
- application bootstrap fd 5 now carries a connected Storage Service endpoint rather than a private-data directory
- application Landlock can grant no direct writable private-data tree (`private_data_directory_fd = -1`)
- application fixture proves fd 5 is an IPC socket and performs private-data I/O only through typed Storage objects
- application process identity is registered before bootstrap completion, so Storage derives authority from trusted runtime identity rather than app payload
- Storage restart leaves old object endpoints permanently stale (`peer_died`)
- new Storage generation receives republished profile roots from App Manager
- restart integration verifies durable PrincipalId/UserId continuity with a fresh ProcessId
- M0 CI now has an explicit `m0` CTest label so M1/M2 supervisor tests cannot contaminate the M0 QEMU signal
- focused M2 GCC, Clang and native AArch64 gates include the App Manager/Storage restart test

M2.2 deliberately does not make object endpoints automatically reconnect across a service crash. Callers must reacquire capabilities from the restarted service.

## Next

M2.3: storage resource accounting and revocation hardening.

Planned scope:

- per-principal live-object quota
- bounded per-principal I/O/accounting limits
- deterministic object invalidation when a profile/root is revoked
- revoke all matching live object slots rather than leaving possession authority alive after policy removal
- adversarial quota-exhaustion and revocation tests
- service-death-during-operation tests
- uninstall/reinstall data-continuity tests without raw directory authority

Key Service/encryption remains separate and follows after the storage authority/resource lifecycle is stable.
