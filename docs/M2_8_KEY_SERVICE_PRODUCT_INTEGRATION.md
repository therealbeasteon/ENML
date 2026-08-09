# M2.8 — Supervised Key Service Product Integration

M2.8 turns the M2.4-M2.7 Key Service, AEAD, rotation, persistence and hierarchy substrate into a supervised system-service path and connects application lifecycle policy to the real App Manager.

This slice deliberately preserves a narrow trust boundary. Public application RPC does not acquire a way to choose `KeyOwner`, `PrincipalId`, `UserId`, `KeyProtectionScope`, root references, provider handles, state paths, or raw key bytes.

## Product shape

```text
trusted package/profile lifecycle
          |
          v
     App Manager
          |
          | private control only
          v
      system.keys
      /    |     \
 identity policy hierarchy
 registry  |     |
          Key Service
              |
      HierarchicalPolicyKeyStore
              |
       PersistentKeyRegistry
              |
      HierarchicalKeyProvider
```

`system.keys` is launched by the existing Supervisor. It receives only fixed private capabilities selected before `execve()`:

- fd 3 — supervisor/service bootstrap and trusted control
- fd 4 — public Key Service endpoint
- fd 5 — already-authorized private Key Service state directory

The service does not parse an application-controlled path to locate durable key state.

## Trusted identity and policy

The Supervisor remains the authority that maps native process evidence to ENML `PeerIdentity`. The Key Service derives every public owner from `RequestContext.peer`:

```text
SCM_CREDENTIALS
      |
      v
IdentityRegistry
      |
      v
PeerIdentity { PrincipalId, UserId, ProcessId }
      |
      v
KeyOwner { PrincipalId, UserId }
```

The public request body has no owner field.

The private control channel supports system-only lifecycle operations:

- ensure a profile hierarchy root exists
- enable application key authority for durable `PrincipalId + UserId`
- disable application key authority for durable `PrincipalId + UserId`
- supervisor process-identity publication/revocation

These operations are intentionally outside the public Key Service API.

## App Manager desired state

App Manager stores desired and generation-local publication state separately:

```text
key_enabled    = desired lifecycle authority
key_published  = known publication into current system.keys generation
```

When a profile is activated, App Manager publishes Key policy from its trusted durable principal/user state. If Key policy was newly enabled but Storage publication then fails, App Manager revokes the newly-added Key policy so profile activation does not leave a half-authorized state.

When `system.keys` restarts, App Manager detects the service generation change, discards `key_published` cache state, reconnects to the new private control capability, and republishes only profiles whose desired `key_enabled` state remains true.

A revoked/uninstalled profile is therefore not accidentally re-enabled merely because the Key Service later restarts.

## Revocation is not destruction

M2.8 keeps authorization and durable cryptographic state as distinct lifecycles.

Disabling application key policy:

1. prevents future create/open operations for that owner;
2. closes all already-minted `KeyObject` endpoints for that owner;
3. leaves durable logical `KeyId` metadata and provider-owned sealed/wrapped key objects intact.

Old object capabilities observe `peer_died` and are never rebound. If trusted lifecycle policy is later enabled again, callers must reacquire a fresh capability.

Uninstall follows the same rule. App Manager revokes Key authority before process teardown, while retaining durable principal and key state unless a future explicit destruction policy says otherwise. Same-signer reinstall can therefore recover the durable principal and, after trusted policy publication, reacquire the retained logical key.

## Hierarchy-backed generation and rotation

M2.8 does not let the product path silently fall back to generic provider key generation.

`HierarchicalPolicyKeyStore` combines:

- lifecycle admission from `ApplicationKeyPolicy`;
- application-root selection from trusted `KeyProtectionBinding`;
- provider generation beneath the M2.7 application root;
- transactional publication into the M2.6 `PersistentKeyRegistry`.

The trusted internal registry hooks are:

```cpp
KeyRegistry::adopt_generated(...)
KeyRegistry::rotate_adopt_generated(...)
PersistentKeyRegistry::adopt_generated(...)
PersistentKeyRegistry::rotate_adopt_generated(...)
```

They never cross public IPC. Provider references remain process-local implementation capabilities.

## Bounded descriptor polling

The Key Service has a fixed logical capacity of 64 object slots, but a fixed policy capacity is not the same thing as 64 open runtime descriptors.

During product integration, a supervised service running with `RLIMIT_NOFILE=32` exposed a Linux `poll(2)` failure because the initial implementation passed the entire 65-entry capacity array to the kernel even when almost every entry had `fd=-1`.

M2.8 now compacts the poll set to exactly the public endpoint plus currently-live KeyObject endpoints. This keeps kernel work proportional to live capabilities and preserves the service resource limit instead of weakening the sandbox to accommodate empty table slots.

## Reference-derived design rationale

The references are used as architectural evidence rather than copied APIs or obsolete cryptographic recipes.

### BitLocker security policy

The BitLocker material separates a bulk data-encryption key from a master key and emphasizes protecting the upstream key so protection can be changed without re-encrypting the whole volume. M2.6-M2.8 use the same *layering principle*: stable logical data keys and durable ciphertext are separate from provider/root protection and lifecycle authorization. BitLocker also describes key-management functionality behind internal system components; M2.8 similarly keeps hierarchy/policy control on a private system channel.

We do **not** copy the historical Vista-era algorithm suite, recovery model, API surface or platform assumptions.

### Symbian OS Architecture Sourcebook

Symbian's key/certificate-store architecture presents centralized security services as a single point of access and places sensitive key-management facilities between application services and lower-level cryptographic implementations. Its System Starter also uses explicit startup policy rather than letting arbitrary services cascade-start the system.

M2.8 follows those structural ideas: one narrow `system.keys` service owns logical key access, App Manager publishes trusted lifecycle state, and Supervisor owns process/service startup. Public applications do not directly manipulate provider roots or key-store implementation details.

### The C++ Programming Language

The C++ reference emphasizes RAII, deterministic ownership, move-only/handle-like resource management, and preserving class invariants rather than leaving partially-constructed resource state.

M2.8 applies that discipline to native handles/channels and to cross-service profile activation: when a newly-enabled Key policy is followed by a failed Storage publication, the newly-created authority is rolled back rather than leaving a half-authorized profile.

### Operating-system isolation references

The operating-system references emphasize process isolation, bounded capabilities and resource limits. M2.8 keeps the privileged key-policy plane on an inherited supervisor-only capability, keeps public application authority identity-derived, and fixes the Key Service poll set rather than increasing the file-descriptor limit to mask an inefficient implementation.

## Host/CI provider caveat

The executable named `system.keys` links the OpenSSL hierarchical provider only when `EMNL_BUILD_OPENSSL_TEST_PROVIDER=ON`.

That provider is a host/CI fixture. Its software root table and test wrapping material are not a TPM, TEE, HSM, secure element, verified-boot key release mechanism, or hardware anti-rollback source.

Production hardware root integration remains a later BSP/security milestone.

## Integration gates

M2.8 adds two product-level tests.

`key_service_supervisor_integration_test` proves:

- real Supervisor bootstrap of `system.keys`;
- trusted process identity publication;
- application policy enable/disable;
- create + encrypt v1;
- hierarchy-backed rotation + encrypt v2;
- live capability death on policy revoke;
- new-open denial while policy is absent;
- service SIGKILL/restart;
- stale old-generation endpoints;
- process identity republish;
- policy absence in a fresh service generation until trusted republish;
- reopening the same durable KeyId and decrypting v1/v2;
- rotation to v3 after restart.

`key_app_manager_policy_integration_test` proves:

- App Manager publishes Key policy from durable profile identity;
- a Key Service generation change triggers automatic replay of still-enabled policy;
- old KeyObject endpoints remain stale after restart;
- uninstall revokes Key authority before application process teardown;
- durable key state is retained by default;
- same-signer reinstall retains PrincipalId and can reacquire the retained key after trusted publication.

The current single-service Supervisor prototype cannot yet give one launched application a single boot-scoped ProcessId and independently broker both Storage and Key Service endpoints. The integration test therefore uses the test process as the Key public client while App Manager owns real lifecycle policy. M2.8 does **not** pretend this is already solved.

## Deliberately deferred

The next architecture-sensitive step is a multi-service service-directory / connection-broker path with one boot-scoped identity authority. It should distribute Storage, Key and later platform-service connections while preserving one `PeerIdentity` for a process.

Do not solve that by registering the same application independently with multiple single-service Supervisor instances and accepting different logical ProcessIds for one process.

Also deferred:

- production TPM/TEE/HSM/secure-element provider
- verified-boot/attestation coupling
- crash-consistent hardware monotonic anti-rollback coupling for `KRG1`
- key backup/recovery policy
- explicit destructive-uninstall policy
