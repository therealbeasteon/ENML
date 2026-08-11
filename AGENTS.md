# ENML OS — Codex Working Instructions

This repository is an incremental implementation of ENML OS. Continue the existing architecture; do not redesign it into Android, a conventional desktop Linux distribution, a monolithic daemon, or a from-scratch kernel.

## Product direction

ENML aims for appliance-like phone behavior, small trusted components, strong process isolation, bounded IPC, stable public APIs, low idle work, fast boot/resume, and a modern phone UX. Linux is the private hardware/process kernel. ENML services, OSIDL, security identities, package/storage/UI/telephony APIs are the public OS personality.

## Hard architectural constraints

- ARM64 is the primary target; x86-64 Linux remains a host implementation tier.
- Linux PID/UID/GID are implementation evidence, never public ENML identity.
- `PrincipalId`, `UserId`, and logical `ProcessId` come from trusted system state; request payloads never establish caller identity.
- One native process must have one boot-scoped logical `ProcessId`. Do not independently allocate different ENML process identities merely because the process connects to multiple services.
- Public IPC uses explicit little-endian serialization. Never serialize native C++ object layout.
- `WireHeaderV1` is a 40-byte logical format. Inline payload remains bounded to 64 KiB and transferred handles to 16 unless a later reviewed ABI revision changes it.
- Handles/native descriptors are move-only RAII. Descriptor inheritance is deny-by-default.
- No shell execution for services and no runtime YAML/JSON/XML parser in the supervisor.
- No universal "system UID" authority model.
- No exceptions across IPC boundaries. Core/system runtime stays no-exceptions/no-RTTI where currently configured.
- Keep normal service hot paths bounded; no hidden thread pools, unbounded queues, or accidental polling loops.
- Fixed policy/table capacity is not permission to pass empty capacity slots to kernel waits. Poll/epoll work should track live resources, not maximum table size.
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
- Public applications never choose `KeyOwner`, `KeyProtectionScope`, provider references, root references or private Key Service control operations.
- Provider references and root references are private process-local implementation handles and must never be serialized as durable logical identity.
- Durable key state stores only provider-owned opaque sealed/wrapped objects. The core must never interpret those blobs as raw keys.
- Key rotation keeps a stable logical KeyId and versioned ciphertext metadata. New encryption uses the current version; retained historical versions exist only for authorized decrypt until an explicit retirement policy is implemented.
- Root hierarchy is downward only: system -> profile -> application. Cross-user or upward edges are security failures.
- A `RootKeyReference` must remain bound to its trusted `KeyProtectionBinding` inside the provider. Do not trust a caller merely because it repeats a binding value.
- Key lifecycle admission is desired system policy. A service restart clears generation-local publication but does not change desired App Manager policy or durable key state.
- Key-policy revocation closes live KeyObject endpoints and blocks acquisition; it is not synonymous with durable key destruction.
- Destroy/revocation, package uninstall, file deletion, key rotation, root-policy revocation, and provider-object cleanup are distinct lifecycle operations.
- A service restart never mutates an old channel/object endpoint into a new generation. Old capabilities remain stale and fresh authority is explicitly reacquired.
- Runtime service reacquisition is limited to the ServiceIds granted at application bootstrap. Do not turn `ServiceBroker` or `PlatformServiceSession` into a general public daemon registry.
- `known_generation` in the runtime-service session is observation metadata only. Trusted Supervisor state chooses the endpoint generation.
- Runtime service requests must continue to validate packet `SCM_CREDENTIALS` against the broker-owned boot-scoped process identity before returning handles.
- App Manager lifecycle/policy reconciliation occurs before endpoint reacquisition; connectivity is not a bypass around Storage/Key admission policy.

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
- M2.8: supervised host/CI `system.keys`, trusted private state/control capabilities, application lifecycle key policy, hierarchy-backed persistent key generation/rotation, generation-aware App Manager policy replay, uninstall revocation without implicit key destruction, stale-capability restart semantics, compact live-endpoint polling, and GCC/Clang/ASan/native-AArch64 product gates.
- M2.9: shared pidfd-backed boot-scoped `ProcessAuthority`, bounded trusted multi-service `ServiceBroker`, application bootstrap v2 typed service-handle transfer, and one `PeerIdentity` across Storage + Keys.
- M2.10: long-lived private `PlatformServiceSession`, exact runtime credential validation, bootstrap ServiceId allow-listing, explicit fresh endpoint reacquisition after Storage/Key restart, unchanged boot-scoped identity, stale old capabilities, bounded App Manager servicing, and end-to-end restart/recovery gates.

Read `docs/M0_STATUS.md`, `docs/M1_STATUS.md`, `docs/M2_STATUS.md`, `docs/M2_0_PRIVATE_STORAGE.md`, `docs/M2_1_STORAGE_SERVICE.md`, `docs/M2_2_STORAGE_PRODUCT_INTEGRATION.md`, `docs/M2_3_STORAGE_REVOCATION_AND_QUOTAS.md`, `docs/M2_6_KEY_PERSISTENCE.md`, `docs/M2_7_KEY_HIERARCHY.md`, `docs/M2_8_KEY_SERVICE_PRODUCT_INTEGRATION.md`, `docs/M2_9_SERVICE_BROKER.md`, and `docs/M2_10_RUNTIME_SERVICE_SESSION.md` before changing those substrates.

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

- Public applications never provide `KeyOwner`, `PrincipalId`, `UserId`, `KeyProtectionScope`, root references, raw provider handles, private control operations or raw long-lived key bytes.
- A `KeyObjectHandle` is an object capability but its operations are still checked against trusted per-message peer identity.
- AES-256-GCM-v1 is the current reviewed service profile; do not invent custom crypto or silently switch profiles.
- The AEAD nonce is provider-owned. `seal()` takes it by non-const reference as an *output*; no caller-supplied or caller-influenced value may ever reach the cipher, and no nonce may repeat under one key. Letting an untrusted caller pin the IV is precisely what made keystream reuse, and then key recovery by XOR alone, possible in CVE-2021-25444. Any future production provider is bound by this, not just the current one.
- The `EKEY` envelope authenticates canonical metadata plus caller AAD.
- `PersistentKeyProvider` returns opaque provider-owned durability blobs. A production provider may use TPM/TEE/HSM sealed objects or secure locators; the OpenSSL provider and fixed wrapping root are test-only.
- `KRG1` persistence is explicit little-endian and bounded. Never serialize `KeyRecord`, `KeyDescriptor`, `ProviderKeyReference`, `RootKeyReference`, or other native C++ layout directly.
- `KBD1` provider binding covers logical KeyId, full PrincipalId, full 64-bit UserId, purpose, rights and the specific retained version so provider blobs cannot be transplanted between records.
- Durable KeyId tombstones prevent a destroyed logical identifier from silently becoming a different key after restart.
- `KeyHierarchy` owns the association between trusted protection bindings and provider root references. Higher layers must not recombine a root reference with an arbitrary binding.
- Profile -> application hierarchy edges require the same durable UserId. A durable application principal cannot be rebound to another user in the same hierarchy policy.
- App Manager keeps `key_enabled` desired state distinct from `key_published` state in the current `system.keys` generation.
- A Key Service restart does not reconnect old KeyObject endpoints. Desired policy is republished to the fresh generation and callers reacquire capabilities.
- Uninstall/revocation disables Key authority before process teardown but retains durable keys unless an explicit destruction policy is added.
- `MonotonicSecurityState` is only an interface boundary. Do not claim anti-rollback until KRG publication is integrated with a real hardware/verified-boot monotonic source using a reviewed crash-consistent protocol.

## Multi-service/runtime-session invariants

- `ProcessAuthority` is the owner of boot-scoped process identity. Service Supervisors publish that authority; they do not independently mint a different logical identity for the same live process.
- `ServiceBroker` is trusted bounded composition machinery, not public discovery. A process must already be attached to a service before `connect_current()` can mint a fresh main endpoint.
- Bootstrap v2 is the reviewed typed source of an application's allowed platform ServiceIds.
- The bootstrap channel may remain as the private runtime session after READY. Its requests use explicit little-endian bounded payloads and successful replies transfer exactly one current-generation endpoint.
- Runtime request sender credentials come from `SCM_CREDENTIALS`; they must match the broker-owned kernel evidence and the already-bound `PeerIdentity`.
- Possessing or inheriting fd 3 is not authority. A forked/unregistered process must not inherit the parent's runtime service access.
- A service generation change preserves the live application's `PeerIdentity` but not old channel/object capability liveness.
- `PlatformServiceSession::acquire()` is a one-shot operation. `service::not_running` is an explicit transient lifecycle state; do not introduce hidden retry threads. Higher layers may retry deliberately.
- App Manager services a bounded number of runtime packets per maintain iteration; preserve low idle work and prevent a chatty application from monopolizing lifecycle processing.
- Uninstall closes the private runtime session before broker/process authority teardown.

## CI boundary

M0 is a frozen foundation gate. M0 CTests carry the explicit `m0` label; the M0 workflow selects that label rather than accidentally running M1/M2 tests. The cross/QEMU gate additionally excludes supervisor/Landlock tests that require native kernel process semantics. Native AArch64 remains the authoritative full-kernel behavior gate.

Do not "fix" an M0 QEMU failure by weakening a later M1/M2 test. First verify whether the test is actually qemu-user-safe. M1 and M2 have their own GCC, Clang and native-AArch64 gates. Key product integration and the M2 broker/runtime line additionally run ASan/UBSan.

M2-only tests must not accidentally match legacy M1 test-name selection. Use explicit M2 CTest labels and keep M1's signal limited to its package/App Manager foundation.

## References

Read `docs/REFERENCE_NOTES_2026_08_08.md` and the milestone-specific design notes before architecture-sensitive changes. References are design evidence, not instructions to copy old vendor APIs, obsolete crypto suites, historical Symbian ABI details, educational kernel architectures, or legacy cellular security mechanisms.

For kernel/BSP work, preserve an upstream-first Linux strategy and small reviewable patches. For C++ core code, prefer type-rich lightweight abstractions, RAII, deterministic ownership and moves. For encryption design, borrow key-hierarchy/boot-integrity principles from historical full-disk-encryption systems without copying their obsolete algorithm choices. For service architecture, preserve centralized trusted policy and explicit process/service boundaries rather than exposing cryptographic implementation choices to apps. Symbian Publish-and-Subscribe/system-server material may guide explicit system-state observation and resource ownership, but ENML keeps its own typed bounded IPC and capability model.

## Current next track: display/compositor/UI foundation

M0, M1, and the M2 storage/key/multi-service runtime substrate are complete. Do not keep broadening `ServiceBroker` merely because another global service could be added to it.

Required direction for the next product track:

1. Introduce the minimal compositor/display service foundation with explicit surface ownership and no application direct access to display devices.
2. Keep compositor, shell/system UI, input routing and application rendering as distinct trust responsibilities where appropriate.
3. Define bounded typed scene/surface primitives and frame submission rather than exposing DRM/KMS/fbdev or Linux device nodes as public app ABI.
4. Preserve frame-deadline awareness, bounded buffering and low idle wakeups from the beginning; phone UX and power efficiency are architectural requirements.
5. Carry trusted application identity into surface ownership and secure-UI decisions without allowing apps to self-claim surface roles.
6. Begin semantic accessibility metadata at the UI API boundary instead of retrofitting it after visual rendering is frozen.
7. Use the supplied BlackBerry/One UI/UX references for workflow, reachability, responsive layout, accessibility and predictable standard components, while deliberately avoiding vendor visual-identity copying.
8. Keep secure lock/permission/system surfaces visually and technically attributable to trusted system principals.
9. Continue GCC/Clang/sanitizer/native-AArch64 validation and add deterministic compositor protocol/ownership tests before hardware-specific display work.
10. Keep hardware/BSP display integration behind the private Linux/driver layer and follow upstream-first kernel/driver practice.

Production TPM/TEE/HSM key providers, verified boot/attestation, hardware monotonic rollback state, telephony/baseband integration and recovery remain separate later tracks; do not fake them in the compositor milestone.

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

Process-sensitive tests must run on GCC, Clang and native AArch64. Do not add them to the M0 qemu-user-safe set unless they are explicitly proven safe there.

### Fast local check on Windows, before pushing

`core/oscore` and `core/oskernel` are portable C++ with header-only `Result` and
`panic`, so the kernel state machines build and run without CMake, without Linux
and without CI. On a Windows box with the VS 2022 Build Tools:

```sh
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /std:c++20 /EHsc /W4 /permissive- /I core\oskernel\include /I core\oscore\include /Fe:t.exe tests\unit\kernel\scheduler_test.cpp core\oskernel\src\scheduler.cpp && t.exe'
```

This is a pre-flight check, not a gate, and it does not replace anything. MSVC
applies no `-Wconversion`/`-Wsign-conversion` and there are no sanitizers here,
so a green run means "no syntax error and the logic holds", not "ready to merge".
Its value is that it costs seconds where a CI round trip costs minutes, and it
catches the mistakes that are most expensive to learn about remotely - aggregate
initialisers that do not match their struct, and state machines whose transitions
were reasoned about rather than executed.

Modules coupled to Linux (`osipc`, `ossandbox`, `osservice`, `osstorage`) will
not build this way. That is expected and is not a failure to investigate.

## Device authority

Port I/O authority is not representable in `DeviceAccessPolicyV1` and must not
be added. A bounded MMIO window can be checked; "the I/O port space" cannot, and
a component holding it is not confined by anything. The microdriver design the
split is drawn from grants exactly this to its user-mode half, because its goal
is fault isolation rather than confinement. ENML's boundary is a security
boundary, so it takes the split and refuses the grant.

A device that masters the bus without an IOMMU reaches all of physical memory
regardless of where its driver runs. Moving a driver out of the kernel isolates
it from the kernel's control flow, not from its memory. `parse_device_access_v1`
therefore rejects any record claiming an `isolated_user` component whose DMA is
`unconfined`, and callers must ask `confined()` rather than testing the
execution domain.

Anything crossing the device boundary goes through OSIDL. The reference
implementation depends on hand-written pointer annotations whose omission
produces an incorrectly marshaled structure - a memory-safety bug manufactured
by the boundary meant to contain one. ENML has generated, typed, bounded wire
formats already; there will be no annotation dialect.

## Secrets and timing

Never compare secret bytes with `==`, `memcmp`, or `std::equal`. All three
short-circuit at the first differing byte, so the time taken reveals how much of
the value the caller already has, and an attacker who can submit candidates and
observe the answer recovers it byte by byte. Use
`os::core::constant_time_equal`. This covers authentication tags, key material,
capability tokens and anything else where being wrong should not be
distinguishable from being nearly right. Digests of public content - package
content digests, boot stage measurements - are not secrets and may be compared
normally.

Wipe key material, nonces, tags and plaintexts before they go out of scope,
using `os::core::secure_zero`. A plain zeroing loop has no observable effect by
the language's rules and a compiler may delete it, leaving the secret in a stack
frame that will be reused or a page that may be swapped.

A production cipher implementation must not use secret-indexed lookup tables.
The table-driven AES construction is precisely the target of the cache attacks
in the references, which recover a full key from a phone with no privileges at
all. Use the platform's cryptographic instructions where they exist and a
data-independent implementation where they do not.

Known gap, recorded rather than papered over: `AeadTag` and `AeadNonce` expose
their bytes as public `std::array` members, so `a.bytes == b.bytes` still
compiles and is still variable-time. The rule above is the control; closing it
properly means wrapping the storage so the naive comparison cannot be written.

Division and modulo are not constant-time instructions on many implementations,
and neither are unaligned accesses. A secret must never influence a divisor, a
shift amount taken from data, or an alignment. This is a separate rule from
avoiding secret-indexed tables and is missed more often, because the code looks
arithmetic rather than table-driven.
