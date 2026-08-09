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

Status: complete and merged.

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

The current Storage Service is synchronous/single-threaded, so per-operation byte ceilings already bound the only request that can be active. Add per-principal outstanding request/byte admission budgets when Storage gains concurrent/asynchronous queues.

See `docs/M2_3_STORAGE_REVOCATION_AND_QUOTAS.md`.

## M2.4 — Key Service foundation and storage-key separation

Status: in progress on `m2-4-key-service-foundation`.

Implemented in the current branch:

- additive `ErrorDomain::key` for stable key-service errors
- opaque 128-bit logical `KeyId`; key ids are locators, never authority
- explicit key purpose and server-held rights metadata
- `KeyProvider` contract returns opaque provider references and has no raw long-lived key-export API
- fixed-capacity `KeyRegistry` keyed by trusted `PrincipalId + UserId`
- cross-owner describe/provider/destroy denial
- destroyed records remain tombstones so a logical key id is not silently reused
- provider failure does not publish a partial registry record
- typed `KeyClient` and move-only `KeyObjectHandle`
- bounded `KeyService` main/object endpoints on stable service ids
- create/open owner identity derives only from `RequestContext.peer`
- possession-based object endpoint for management authority; public KeyId alone is insufficient
- destroying a key invalidates every already-minted object endpoint for that key
- fork/inherited-main-channel adversarial test uses per-message `SCM_CREDENTIALS` to prove a second principal cannot open the first principal's key
- dedicated GCC, Clang and native AArch64 key gates

Not implemented yet and therefore not claimed:

- production `system.keys` executable/provider
- encryption/decryption operations
- a frozen AEAD crypto profile
- key-registry/provider persistence across service or device restart
- rotation with retained old versions for existing ciphertext
- hardware-backed TPM/TEE/HSM sealing
- boot measurement/attestation/recovery policy

See `docs/M2_4_KEY_SERVICE_FOUNDATION.md`.
