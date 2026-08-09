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

Status: complete and merged.

Implemented:

- real supervised `system.storage` executable
- trusted root publication over the private supervisor control channel
- App Manager retains durable profile identity/root state and republishes it when the Storage service generation changes
- application bootstrap fd 5 carries a connected Storage Service endpoint rather than a private-data directory
- application Landlock can grant no direct writable private-data tree (`private_data_directory_fd = -1`)
- application fixture proves fd 5 is an IPC socket and performs private-data I/O only through typed Storage objects
- application process identity is registered before bootstrap completion, so Storage derives authority from trusted runtime identity rather than app payload
- Storage restart leaves old object endpoints permanently stale (`peer_died`)
- new Storage generation receives republished profile roots from App Manager
- restart integration verifies durable PrincipalId/UserId continuity with a fresh ProcessId
- M0 CI uses an explicit `m0` CTest label so M1/M2 supervisor tests cannot contaminate the M0 QEMU signal
- M2 GCC, Clang and native AArch64 gates include the App Manager/Storage restart test

M2.2 deliberately does not reconnect object endpoints after a service crash. Callers reacquire capabilities from the restarted service.

See `docs/M2_2_STORAGE_PRODUCT_INTEGRATION.md`.

## M2.3 — resource accounting and revocation hardening

Status: implementation complete; final PR gates pending before merge.

Implemented:

- additive stable `principal_object_limit` storage error for profile-local object exhaustion
- 16-object per-profile budget keyed by durable `PrincipalId + UserId`, beneath the 64-object global table
- server-side quota enforcement on every object-minting path: private root, child file, and child directory
- a saturated profile receives `principal_object_limit` while the global table still has capacity
- a second process/principal sharing the inherited transport can still mint its own root, proving quota isolation uses per-message identity rather than connection ownership
- trusted private-root revocation closes every already-minted object endpoint for the profile
- public `open_private_root()` fails with `root_not_registered` while policy is absent
- republishing policy requires a fresh capability; stale bearer endpoints remain `peer_died`
- App Manager tracks enabled-vs-published Storage profile policy across service generations
- package uninstall disables and revokes Storage policy before process teardown while retaining the durable principal and private-data directory
- same-signer reinstall explicitly republishes the retained profile on the next trusted launch; revoked capabilities are never resurrected
- integration proof that uninstall revokes Storage authority without deleting private data
- GCC, Clang and native AArch64 Storage gates cover quota, root revocation, service restart and uninstall revocation

### I/O accounting decision

The current Storage Service is single-threaded and synchronous: it dispatches at most one object request at a time, and each read/write/atomic operation is already bounded by `max_storage_io_bytes` / `max_storage_atomic_bytes` plus the 64 KiB OSIP message ceiling. A separate "outstanding bytes per principal" counter would not enforce an additional property in this execution model. Add such accounting when Storage gains concurrent/asynchronous requests or queued background I/O, where multiple operations can actually be outstanding.

See `docs/M2_3_STORAGE_REVOCATION_AND_QUOTAS.md`.

## Next

M2.4: Key Service foundation and storage-key separation.

Initial scope:

- define opaque key identities/handles; never expose raw long-lived key bytes through public app APIs
- separate Storage authority from key authority
- model root/system/profile/application key hierarchy without binding the public ABI to a specific TPM/TEE vendor
- use authenticated encryption with an explicit current cryptographic profile rather than copying historical BitLocker algorithms
- support key versioning/rotation and cryptographic deletion semantics
- keep unlock credentials distinct from raw data-encryption keys
- add bounded, identity-authenticated Key Service IPC and adversarial caller-identity tests

Boot measurement/verified-boot integration and hardware-backed sealing remain later hardware/BSP work; M2.4 begins with the userspace service contracts and software-backed test provider.
