# M2.3 Storage Revocation and Quotas

M2.3 hardens the lifetime of Storage authority after M2.2 moved application private data behind `system.storage`.

## Ownership key

Storage policy and resource accounting use:

```text
PrincipalId + UserId
```

not Linux PID/UID/GID and not logical `ProcessId` alone. `ProcessId` identifies one execution generation; private-data ownership survives normal process restart and package update.

## Object quotas

The service retains a fixed global table of 64 object slots and applies a smaller fixed budget of 16 live objects to each `(PrincipalId, UserId)` profile.

Every capability-minting path performs the same server-side check before allocating a slot:

```text
open_private_root
open_file
open_directory
        |
        v
count live slots owned by PrincipalId + UserId
        |
        +-- >= 16 --> storage::principal_object_limit
        |
        v
find free global slot
        |
        +-- none --> storage::object_limit
```

Client-side checks are not trusted for enforcement. The authoritative accounting is the service's object table.

The quota integration test fills one profile's complete object budget, verifies its next request receives `principal_object_limit` while the global table still has room, then uses a second process over the inherited transport. Per-message `SCM_CREDENTIALS` maps that sender to a different profile, which can still acquire its own root capability.

## Root revocation

A private root is policy. A directory/file object endpoint is a bearer capability derived from that policy. Removing policy must invalidate already-derived authority rather than merely preventing future root lookup.

Trusted root revocation therefore performs both operations:

1. remove `(PrincipalId, UserId)` from `PrivateRootRegistry`;
2. clear every live object slot owned by the same profile.

Clearing the service-side endpoint causes old client capabilities to observe `peer_died`.

Re-registering the root later does not reconnect those endpoints. A caller must explicitly reacquire a fresh root/object capability.

## Uninstall semantics

Package uninstall is not data deletion.

For each retained application profile, App Manager distinguishes:

```text
storage_enabled
storage_published
```

Uninstall commits the package's no-active-generation state, disables Storage policy, revokes a currently published root, invalidates all Storage objects for that profile, and revokes running process identities. It deliberately retains:

- durable application PrincipalId;
- already-authorized private-data directory held by trusted App Manager state;
- private data itself;
- historical package/signer ownership needed for safe reinstall/update semantics.

A same-signer reinstall may become launchable again. The next trusted launch explicitly republishes the retained profile to the current Storage generation before giving the new process a Storage endpoint. The new process gets a fresh `ProcessId`; the durable PrincipalId/UserId remains the private-data ownership key.

## Storage service restart

A service restart is not policy revocation, but the process-local registries disappear with the dead generation. App Manager detects a new Storage generation, marks previously published profiles unpublished, and republishes only profiles whose policy remains enabled.

Therefore an application uninstalled while Storage was alive cannot silently regain a root merely because Storage later restarts.

Object capabilities from the dead generation always remain dead.

## I/O accounting

M2.3 does not add a synthetic outstanding-byte counter. `StorageService` is currently synchronous and single-threaded: only one request is dispatched at a time, and per-operation payloads are already bounded by the Storage constants and the OSIP 64 KiB ceiling.

When Storage gains concurrent/asynchronous requests or queued background I/O, add per-principal outstanding-operation and outstanding-byte budgets at the queue admission point. That is the point where such accounting begins to enforce a new resource-isolation property.

## Security properties demonstrated

- possession of a shared transport does not merge principals;
- one profile cannot consume another profile's object budget;
- revocation invalidates already-issued bearer capabilities;
- policy re-enable never resurrects a stale endpoint;
- uninstall revokes authority without deleting data;
- service restart republishes only still-enabled policy;
- no raw private-data directory descriptor is returned to applications.
