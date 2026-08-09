# ENML OS — Codex Working Instructions

This repository is an incremental implementation of ENML OS. Continue the existing architecture; do not redesign it into Android, a conventional desktop Linux distribution, a monolithic daemon, or a from-scratch kernel.

## Product direction

ENML aims for appliance-like phone behavior, small trusted components, strong process isolation, bounded IPC, stable public APIs, low idle work, fast boot/resume, and a modern phone UX. Linux is the private hardware/process kernel. ENML services, OSIDL, security identities, package/storage/UI/telephony APIs are the public OS personality.

## Hard architectural constraints

- ARM64 is the primary target; x86-64 Linux remains a host implementation tier.
- Linux PID/UID/GID are implementation evidence, never public ENML identity.
- `PrincipalId`, `UserId`, and logical `ProcessId` come from trusted system state; request payloads never establish caller identity.
- Public IPC uses explicit little-endian serialization. Never serialize native C++ object layout.
- `WireHeaderV1` is a 40-byte logical format. Inline payload remains bounded to 64 KiB and transferred handles to 16 unless a later reviewed ABI revision changes it.
- Handles/native descriptors are move-only RAII. Descriptor inheritance is deny-by-default.
- No shell execution for services and no runtime YAML/JSON/XML parser in the supervisor.
- No universal "system UID" authority model.
- No exceptions across IPC boundaries. Core/system runtime stays no-exceptions/no-RTTI where currently configured.
- Keep normal service hot paths bounded; no hidden thread pools, unbounded queues, or accidental polling loops.
- Generated OSIDL code is ABI/convenience machinery, not the authorization boundary.
- Application launch is package-based, never arbitrary-path-based. Apps do not choose executable paths, native credentials, `PrincipalId`, active generation, content digest, storage root, key identity, or sandbox policy.
- Staging a package generation does not activate it. Running processes stay pinned to the immutable generation that created them.
- Per-user application PrincipalIds are durable identities and are not recycled casually across update/uninstall/reinstall.
- Uninstall is not synonymous with data/key destruction. Launch state, process authority, immutable code retention, principal history, private data, backup state, and keys are separate resources.
- Public app storage APIs expose neither Linux fd numbers nor absolute Linux paths as stable ABI.
- Storage traversal stays rooted in trusted object authority; never reintroduce caller-controlled absolute `open()` paths.
- Storage root selection is based on trusted `RequestContext.peer`; public Storage requests never claim PrincipalId/UserId/native identity.
- Storage object rights are authoritative on the server. Client-side rights checks are ergonomic only.
- Object delegation may only reduce rights.
- Revoking a Storage profile invalidates all already-minted object endpoints for that `PrincipalId + UserId`.
- RPC error responses never transfer handles. Successful handle-bearing messages must keep flags/count/SCM_RIGHTS consistent.
- Long-lived cryptographic keys must never become public raw-byte application APIs.
- `KeyId` is a locator, not authority. Every Key Service operation remains bound to trusted `PrincipalId + UserId` and server-held rights.
- Provider references and root references are private process-local implementation handles and must never be serialized as durable logical identity.
- Durable key state stores only provider-owned opaque sealed/wrapped objects. The core must never interpret those blobs as raw keys.
- Key rotation keeps a stable logical KeyId and versioned ciphertext metadata. New encryption uses the current version; retained historical versions exist only for authorized decrypt until an explicit retirement policy is implemented.
- Root hierarchy is downward only: system -> profile -> application. Cross-user or upward edges are security failures.
- A `RootKeyReference` must remain bound to its trusted `KeyProtectionBinding` inside the provider. Do not trust a caller merely because it repeats a binding value.
- Destroy/revocation, package uninstall, file deletion, key rotation, root-policy revocation, and provider-object cleanup are distinct lifecycle operations.

## Completed implementation

- M0.0-M0.10: build/oscore, bounded OSIP/Channel, OSIDL, typed Echo, supervisor lifecycle, trusted identity, Linux sandbox, adversarial/resource gates, native AArch64 and independent cross/QEMU validation.
- M1.0-M1.5: signer-bound package identity, hostile bounded manifests, durable staging/activation, generation-bound App Manager launch, durable per-user principals/application sandbox, update/uninstall/revocation/generation retention.
- M2.0: descriptor-rooted private storage with bounded UTF-8 `RelativePath`, typed `File`/`Directory`, `O_NOFOLLOW` confinement and crash-resistant atomic replace.
- M2.1: identity-rooted Storage Service, typed directory/file object capabilities, bounded handle transfer, server-authoritative rights reduction and adversarial identity tests.
- M2.2: supervised `system.storage`, App Manager profile publication/republication, no direct private-data directory in apps, generation-bound Storage capabilities and restart behavior.
- M2.3: per-profile object quotas, quota isolation by `PrincipalId + UserId`, deterministic root/object revocation, uninstall Storage-policy revocation while retaining data/principal continuity, and fresh capability reacquisition on reinstall/re-enable.
- M2.4: typed Key Service AEAD path, opaque KeyIds/provider references, trusted identity enforcement, bounded `EKEY` AES-256-GCM-v1 envelopes, tamper/wrong-AAD/wrong-owner/inherited-fd tests.
- M2.5: stable logical-key rotation, explicit rotate right, up to eight retained versions, historical decrypt, current-version encryption and key-wide destruction/revocation.
- M2.6: provider-wrapped persistence and durable `KRG1` registry with canonical owner/key/version binding, transactional publication, durable tombstones, provider restart recovery and end-to-end Key Service restart tests.
- M2.7: trusted system/profile/application protection scopes, strict downward hierarchy policy, opaque provider root references, `HierarchicalKeyProvider`, bounded `KeyHierarchy`, and `MonotonicSecurityState` interface. M2.7 is a provider/security contract; it does not claim a production TPM/TEE/HSM implementation or host-filesystem anti-rollback.

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, `docs/M2_STATUS.md`, `docs/M2_0_PRIVATE_STORAGE.md`, `docs/M2_1_STORAGE_SERVICE.md`, `docs/M2_2_STORAGE_PRODUCT_INTEGRATION.md`, `docs/M2_3_STORAGE_REVOCATION_AND_QUOTAS.md`, `docs/M2_6_KEY_PERSISTENCE.md`, and `docs/M2_7_KEY_HIERARCHY.md` before changing those substrates.

## Storage invariants

- `RelativePath` is fixed-capacity UTF-8 and rejects absolute paths, empty segments, `.`/`..`, NUL, backslash, malformed UTF-8 and overlong forms.
- `PrivateRoot` originates only from an already-authorized directory handle.
- Descendant traversal is descriptor-relative, segment-by-segment and `O_NOFOLLOW`.
- Final `File` objects must be regular files; FIFOs/sockets/devices/directories are rejected.
- `atomic_replace()` remains bounded same-directory temp/write/fsync/rename/parent-fsync.
- Root ownership and Storage quota keys are durable `PrincipalId + UserId`, not `ProcessId`.
- Storage object endpoints are bearer capabilities and rights stay server-side.
- One profile is limited to 16 live Storage object slots beneath the 64-slot global table.
- Root revocation removes future root lookup and closes every already-minted object endpoint for the profile.
- Republish/reinstall never resurrects an old object endpoint; callers reacquire fresh capabilities.
- App Manager may retain private data and durable principal state after uninstall while live Storage authority remains revoked.
- Current Storage I/O is synchronous/single-threaded and per-operation bounded. Add outstanding byte/request budgets when concurrent/asynchronous Storage queues are introduced, not as inert counters today.

## Key-service invariants

- Public applications never provide `KeyOwner`, `PrincipalId`, `UserId`, `KeyProtectionScope`, root references, raw provider handles, or raw long-lived key bytes.
- A `KeyObjectHandle` is an object capability but its operations are still checked against trusted per-message peer identity.
- AES-256-GCM-v1 is the current reviewed service profile; do not invent custom crypto or silently switch profiles.
- The `EKEY` envelope authenticates canonical metadata plus caller AAD.
- `PersistentKeyProvider` returns opaque provider-owned durability blobs. A production provider may use TPM/TEE/HSM sealed objects or secure locators; the OpenSSL provider and fixed wrapping root are test-only.
- `KRG1` persistence is explicit little-endian and bounded. Never serialize `KeyRecord`, `KeyDescriptor`, `ProviderKeyReference`, `RootKeyReference`, or other native C++ layout directly.
- `KBD1` provider binding covers logical KeyId, full PrincipalId, full 64-bit UserId, purpose, rights and the specific retained version so provider blobs cannot be transplanted between records.
- Durable KeyId tombstones prevent a destroyed logical identifier from silently becoming a different key after restart.
- `KeyHierarchy` owns the association between trusted protection bindings and provider root references. Higher layers must not recombine a root reference with an arbitrary binding.
- Profile -> application hierarchy edges require the same durable UserId. A durable application principal cannot be rebound to another user in the same hierarchy policy.
- `MonotonicSecurityState` is only an interface boundary. Do not claim anti-rollback until KRG publication is integrated with a real hardware/verified-boot monotonic source using a reviewed crash-consistent protocol.

## CI boundary

M0 is a frozen foundation gate. M0 CTests carry the explicit `m0` label; the M0 workflow selects that label rather than accidentally running M1/M2 tests. The cross/QEMU gate additionally excludes supervisor/Landlock tests that require native kernel process semantics. Native AArch64 remains the authoritative full-kernel behavior gate.

Do not "fix" an M0 QEMU failure by weakening a later M1/M2 test. First verify whether the test is actually qemu-user-safe. M1 and M2 have their own GCC, Clang and native-AArch64 gates.

## References

Read `docs/REFERENCE_NOTES_2026_08_08.md` before architecture-sensitive changes. References are design evidence, not instructions to copy old vendor APIs, obsolete crypto suites, historical Symbian ABI details, educational kernel architectures, or legacy cellular security mechanisms.

For kernel/BSP work, preserve an upstream-first Linux strategy and small reviewable patches. For C++ core code, prefer type-rich lightweight abstractions, RAII, deterministic ownership and moves. For encryption design, borrow key-hierarchy/boot-integrity principles from historical full-disk-encryption systems without copying their obsolete algorithm choices.

## Current next milestone: M2.8 supervised Key Service product integration

M2.4-M2.7 established the Key Service protocol, AEAD, rotation, persistence and root-provider contract. The next slice should turn those pieces into a real supervised product service without weakening trusted identity.

Required direction:

1. Add a real `system.keys` executable supervised through the existing lifecycle machinery; do not create a general daemon framework or second init system.
2. Construct the service with a durable `PersistentKeyRegistry` and a provider selected by trusted platform configuration. The host OpenSSL provider remains test-only.
3. Establish the M2.7 `KeyHierarchy` from trusted system policy. Public application requests never select system/profile/application scope or root references.
4. Add a private system-control path for publishing/revoking profile/application key policy, analogous in spirit to Storage control but with key-specific semantics and no raw key transfer.
5. Bind application policy to the existing durable App Manager `PrincipalId + UserId`; package payloads and app request bytes do not establish key ownership.
6. On Key Service restart, republish enabled application/profile policy and reacquire provider roots through binding-aware provider operations. Old public object endpoints stay stale and callers reacquire them.
7. Uninstall/revocation must disable future key acquisition and stale live capabilities without silently deleting retained user data or long-lived keys unless an explicit destruction policy says so.
8. Preserve M2.4-M2.6 public Key Service wire behavior unless an additive operation is required.
9. Keep hardware attestation, verified-boot integration, actual TEE/HSM implementation and KRG anti-rollback coupling out of this slice except for the narrow interfaces required to avoid later ABI breakage.
10. Add GCC, Clang and native AArch64 integration tests covering service restart, policy republish, wrong-principal acquisition, uninstall/revocation, and stale-capability behavior.

## Build and test

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset host-asan
cmake --build --preset host-asan
ctest --preset host-asan
```

For Clang:

```sh
cmake -S . -B build/host-clang -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build/host-clang
ctest --test-dir build/host-clang --output-on-failure
```

M2 process-sensitive tests must run on GCC, Clang and native AArch64. Do not add them to the M0 qemu-user-safe set unless they are explicitly proven safe there.
