# M2.7 — Key hierarchy and root-provider security contract

M2.7 defines the first ENML contract for moving durable Key Service protection from a host-only software wrapping key toward a real system/profile/application hierarchy backed by a production security provider.

It does **not** claim that a TPM, TEE, HSM, verified-boot chain, or hardware anti-rollback source is implemented yet.

## Trusted logical hierarchy

The hierarchy is intentionally small:

```text
system root
    ↓
user/profile root
    ↓
application root
    ↓
versioned application data keys
```

Every root is associated with a trusted `KeyProtectionBinding` containing a scope and `KeyOwner { PrincipalId, UserId }`. These values come from system policy. A public Key Service request never chooses another principal's hierarchy binding.

Valid root edges are deliberately narrow:

- system -> user/profile
- user/profile -> application only when both bindings carry the same durable `UserId`

System -> application shortcuts, application -> profile/system escalation, profile -> system escalation, and cross-user profile -> application edges are rejected.

## Opaque provider roots

`RootKeyReference` is a provider-private process-local handle. It is not durable identity and is not public ABI.

`HierarchicalKeyProvider` extends the M2.6 persistent provider boundary with four root operations:

- acquire the provider-owned system root for a trusted system binding
- acquire an idempotent child root beneath an existing root and validated binding edge
- generate an ordinary opaque `ProviderKeyReference` beneath an application root
- destroy a provider-owned root

There is no raw-root-key export operation.

A provider must internally bind every root reference to its `KeyProtectionBinding` and reject a mismatched reference/binding pair. Passing a binding argument back to the provider is not a substitute for provider-side authority state.

A production implementation may map these operations to TPM/TEE/HSM sealed objects, hardware key slots, or another reviewed provider mechanism. M2.7 does not force a vendor-specific API into Key Service.

## Bounded `KeyHierarchy`

The core `KeyHierarchy` object pairs trusted bindings with opaque root references so higher layers never select or recombine raw root references themselves.

Current fixed bounds are:

- one system root
- 16 user/profile roots
- 64 application roots

Within one hierarchy instance, a profile root is unique per `UserId`, and an application root is unique per `PrincipalId + UserId`. Replaying the exact same trusted policy is idempotent. Attempting to bind the same user to a different profile root or the same application principal to another user is rejected as a hierarchy conflict.

An application data key can only be generated after its trusted profile and application roots exist. The result is the same opaque `ProviderKeyReference` already consumed by the M2.4-M2.6 Key Registry path; M2.7 does not add a raw-key side channel.

## Monotonic security-state boundary

`SecurityEpoch` and `MonotonicSecurityState` define the minimum interface expected from a future production anti-rollback source:

```text
current() -> epoch N
advance(expected=N) -> atomically verify N and publish N+1
```

The operation is compare-and-advance rather than an arbitrary counter write, so stale state cannot request a regression or skip policy checks.

This interface alone does **not** make `KRG1` rollback-resistant. Combining a hardware monotonic epoch with the filesystem snapshot requires a separate crash-consistent protocol that handles every power-loss point without either accepting rollback or permanently bricking valid state. M2.7 intentionally leaves that integration for a reviewed later slice.

## Verification

`key_hierarchy_test` covers:

- valid system -> profile -> application descent
- system -> application shortcut rejection
- upward hierarchy rejection
- cross-user profile -> application rejection
- hierarchy initialization requirement
- idempotent replay of trusted system/profile/application policy
- conflicting system root rejection
- conflicting profile binding for the same UserId
- application principal rebind to another UserId rejection
- application data-key generation only beneath an acquired application root
- missing-root failure before provider key generation
- compare-and-advance monotonic-state semantics and stale-epoch rejection

The hierarchy test is part of the M2 Key Service GCC, Clang, and native AArch64 gates. The inherited M0, M1, Storage, AEAD, rotation, and persistence gates remain unchanged.

## Deliberately deferred

M2.7 does not yet:

- provide a production `HierarchicalKeyProvider`
- launch a supervised `system.keys` product service using a hardware provider
- publish profile/application root policy from App Manager
- migrate M2.6 provider blobs under a real hardware root
- bind `KRG1` snapshots to a hardware monotonic epoch
- implement measured boot, attestation, recovery, or device-specific TEE policy

Those must be added without weakening the existing rule that logical owner identity comes from trusted system state and raw long-lived keys never become application API.
