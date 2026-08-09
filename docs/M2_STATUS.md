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

## M2.4 — typed Key Service + authenticated encryption

Status: complete and merged.

Implemented:

- additive stable `ErrorDomain::key`
- opaque 128-bit logical `KeyId`; ids are locators, never authority
- `KeyProvider` abstraction with opaque provider references and no public raw long-lived key export
- fixed-capacity `KeyRegistry` keyed by trusted `PrincipalId + UserId`
- typed `KeyClient` and move-only `KeyObjectHandle`
- bounded `KeyService` main/object endpoints with create/open/destroy/encrypt/decrypt operations
- exact trusted `PeerIdentity` revalidation on every object request, including inherited-fd rejection
- explicit AES-256-GCM-v1 crypto profile for the current service slice
- explicit bounded `EKEY` envelope with authenticated canonical header plus caller AAD
- wrong owner, wrong rights, wrong key/version, malformed envelope, wrong AAD, tag/ciphertext tamper and destroyed-key rejection
- OpenSSL-backed provider exists only as a host/CI test provider, not a production hardware root
- GCC, Clang and native AArch64 Key Service gates

The service never accepts caller-supplied owner identity and never sends raw long-lived key bytes through public IPC.

## M2.5 — logical key rotation and retained versions

Status: complete and merged.

Implemented:

- explicit `rotate` right and typed `KeyObjectHandle::rotate()`
- up to eight retained provider-key versions per logical `KeyId`
- monotonic current version
- new encryption restricted to the current version
- historical ciphertext decryption through retained older versions
- stale object descriptor convergence to the current version on subsequent use
- key-wide destruction erases every retained provider version and revokes every live object endpoint
- no silent KeyId reuse and no implicit historical-version retirement
- bounded `version_limit` failure instead of unbounded retention
- cross-process tests proving v1 ciphertext survives v2 rotation and both versions remain decryptable
- GCC, Clang and native AArch64 validation plus unchanged M0/M1/M2 Storage gates

## M2.6 — durable provider-wrapped key persistence

Status: complete and merged.

Implemented:

- `PersistentKeyProvider` internal durability contract for opaque sealed/wrapped provider objects
- no serialized process-local `ProviderKeyReference` and no raw long-lived key bytes in registry state
- CI OpenSSL provider wraps test keys with authenticated AES-256-GCM under an explicitly test-only wrapping root
- provider blobs authenticate a canonical `KBD1` binding containing KeyId, full PrincipalId, full 64-bit UserId, purpose, rights and specific retained version
- bounded explicit little-endian `KRG1` Key Registry snapshot
- maximum 128 logical keys, eight retained versions per live key and 256-byte provider blobs
- already-authorized state-directory handle rather than caller-controlled absolute state path
- `O_NOFOLLOW`/`O_EXCL` temporary publication with stale-temp cleanup
- temp write -> fsync -> atomic rename -> directory fsync publication
- create/rotate candidate rollback before replacement and replacement-aware handling of a late directory-fsync error
- logical destroy/tombstone publication before best-effort physical provider-object deletion
- destroyed KeyIds persist across restart and cannot be silently reused
- provider restart test with binding/tamper/truncation rejection
- registry restart test with >32-bit UserId, v1/v2 historical decrypt, v3 post-restart rotation, tombstone recovery, 0600 snapshot mode and temp-name symlink non-following
- end-to-end cross-process Key Service restart test using a fresh provider and fresh `PersistentKeyRegistry`

See `docs/M2_6_KEY_PERSISTENCE.md`.

M2.6 deliberately does not claim production TPM/TEE/HSM protection, measured boot, attestation, recovery policy or filesystem rollback resistance.

## M2.7 — key hierarchy and root-provider security contract

Status: complete and merged.

Implemented:

- trusted `KeyProtectionScope` values for system, user/profile and application roots
- `KeyProtectionBinding` tied to trusted `KeyOwner { PrincipalId, UserId }`
- explicit downward hierarchy rule: system -> profile -> application
- profile -> application requires the same durable UserId
- shortcut, upward and cross-user hierarchy edges are rejected
- opaque process-local `RootKeyReference`; no root-key byte export API
- `HierarchicalKeyProvider` extending the M2.6 persistence provider contract
- idempotent provider root-acquisition contract for system and child roots
- provider-side binding requirement for every root reference
- fixed-capacity `KeyHierarchy` pairing trusted bindings with root references: one system root, 16 profile roots, 64 application roots
- profile uniqueness per UserId and application uniqueness per PrincipalId + UserId
- conflicting root-policy replay is rejected while exact replay is idempotent
- application data-key generation only after trusted profile/application root acquisition
- `SecurityEpoch` and compare-and-advance `MonotonicSecurityState` interface for a future hardware anti-rollback source
- tests for initialization, valid descent, upward/cross-user rejection, principal rebind rejection, missing roots, idempotent policy replay and stale monotonic epochs
- GCC, Clang and native AArch64 M2 Key gates

See `docs/M2_7_KEY_HIERARCHY.md`.

M2.7 defines a security contract, not a fake hardware implementation. The host OpenSSL provider remains test-only, and KRG snapshots are not claimed rollback-resistant.

## M2.8 — supervised Key Service product integration

Status: complete and merged.

Implemented:

- real supervised host/CI `system.keys` executable using the existing Supervisor bootstrap/lifecycle path
- private already-authorized Key Service state-directory capability at fixed fd 5; no public/caller-controlled state path
- private Key Service lifecycle control for profile-root ensure, application enable and application disable
- supervisor identity register/unregister handled on the same inherited private control plane
- `ApplicationKeyPolicy` keyed by durable trusted `PrincipalId + UserId`
- policy disable denies future create/open and closes all live `KeyObject` endpoints for that owner without destroying durable key state
- hierarchy-backed product store so v1 creation and later rotation generate provider material beneath the trusted application root
- trusted internal generated-provider-key adoption into `KeyRegistry`/`PersistentKeyRegistry`; provider/root references remain private
- App Manager desired-vs-published Key policy state with service-generation-aware replay
- App Manager revokes Key authority before Storage/process teardown on uninstall
- same-signer reinstall retains durable principal/key state and requires a fresh trusted policy publication/capability acquisition
- Storage + Key profile activation rollback avoids leaving a newly-installed profile half-authorized when later publication fails
- supervised restart gate proving old KeyObject endpoints remain stale, process identity is republished separately from key policy, durable KeyId/v1/v2 ciphertext survive, and rotation continues after restart
- App Manager integration gate proving automatic Key-policy replay after `system.keys` generation change and uninstall/reinstall authority semantics
- Key Service polls only live public/object descriptors rather than passing its entire 64-slot policy capacity to `poll(2)`, preserving the sandbox `RLIMIT_NOFILE` and keeping kernel work proportional to active capabilities
- GCC, Clang, ASan/UBSan and native AArch64 Key product gates

See `docs/M2_8_KEY_SERVICE_PRODUCT_INTEGRATION.md`.

Production TPM/TEE/HSM/secure-element roots, verified-boot/attestation coupling and crash-consistent hardware monotonic anti-rollback remain later security/BSP work.

## M2.9 — identity-preserving multi-service broker

Status: complete and merged.

Implemented:

- one fixed-capacity pidfd-backed boot-scoped `ProcessAuthority` shared by participating Supervisors
- one live native execution maps to one logical `PeerIdentity` across Storage, Keys and later platform services
- bounded trusted `ServiceBroker` with eight registered system services and 64 attached processes
- exact ServiceId/Supervisor/shared-authority validation at trusted broker construction
- transactional multi-service process attachment with rollback and explicit ownership of broker publications
- service-local identity publication using duplicated pidfds; numeric Linux PID reuse cannot revive old authority
- application bootstrap v2 with explicit little-endian typed `(ServiceId, endpoint)` transfer over `SCM_RIGHTS`
- App Manager broker-mode launch that publishes Storage/Key policy, attaches one child identity to both services and transfers both current-generation endpoints
- old service-generation endpoints remain permanently stale while explicit broker reacquisition returns a fresh endpoint under the same `PeerIdentity`
- real sandboxed application gate performing Storage I/O and Key Service AEAD under exactly the same trusted identity
- GCC, Clang, ASan/UBSan and native AArch64 broker/product validation

See `docs/M2_9_SERVICE_BROKER.md`.

M2.9 is deliberately a narrow trusted broker, not a public general-purpose service bus.

## M2.10 — runtime platform-service session

Status: implementation complete; merge is gated on final branch-wide CI.

Implemented:

- the private bootstrap-v2 channel remains alive after READY as a long-lived application runtime-service session
- application-side move-only `PlatformServiceSession` / `PlatformServiceEndpoint`
- fixed private `Acquire(ServiceId, known_generation)` wire contract with explicit little-endian serialization and one successful endpoint handle
- bootstrap ServiceIds remain the application-visible allow-list; runtime acquisition cannot discover or expand to arbitrary system services
- App Manager validates per-message `SCM_CREDENTIALS` against the broker-owned boot-scoped process record before returning any endpoint
- `ServiceBroker::connect_current()` returns the trusted current Supervisor generation plus a distinct current-generation channel
- `known_generation` is observation metadata only; callers cannot select an old or fabricated service generation
- old main/object capabilities remain stale after service death and are never mutated or rebound behind the caller
- App Manager reconciles generation-local Storage/Key policy before servicing reacquisition
- explicit one-shot `not_running` during restart; callers may retry without hidden reconnect threads or background queues
- at most four runtime-session packets are serviced per application per App Manager `maintain()` iteration
- uninstall closes the runtime session before broker identity revocation and process teardown
- end-to-end gate kills `system.keys`, reopens the same durable KeyId and decrypts pre-crash ciphertext through a fresh endpoint, then kills `system.storage` and reacquires a fresh private-root capability
- the integration gate proves the application's `PeerIdentity` remains unchanged in both Supervisors, the broker and `ProcessAuthority` across both restarts
- GCC, Clang, ASan/UBSan and native AArch64 runtime-session gates

See `docs/M2_10_RUNTIME_SERVICE_SESSION.md`.

## Next track — display/compositor/UI foundation

M2 closes the private storage, cryptographic service and identity-preserving multi-service runtime-connectivity substrate. The next product track should begin the display/compositor/UI foundation: semantic surfaces, compositor ownership, input routing, frame scheduling and the trusted shell/system-UI boundary.

Production hardware-backed key roots, verified-boot/attestation coupling and hardware monotonic anti-rollback remain later security/BSP work; the host OpenSSL provider must not be presented as those mechanisms.
