# M1.5 — Update, Uninstall, Revocation, and Generation Retention

M1.5 closes the first package/application lifecycle loop built across M1.0-M1.4. The milestone separates four concepts that must not be collapsed into one destructive "uninstall" operation:

1. future launch eligibility;
2. live-process authority;
3. immutable code-generation retention;
4. durable application identity/private user data.

That separation is intentional. It prevents an update or uninstall from accidentally changing security identity, deleting code still used by a running process, or treating data destruction as a side effect of package-state mutation.

## Durable uninstall state

`PackageRegistry::uninstall()` clears the active generation for the exact signer-bound `ApplicationIdentity`.

It deliberately retains:

- `PackageId -> SignerLineageId` ownership;
- historical `PackageGenerationRecord` values;
- monotonic generation history.

Therefore a textual PackageId does not become claimable by a different signer after uninstall, and a same-signer reinstall/update continues at a higher generation rather than resetting package history.

The existing EPR1 snapshot already represents this state: an application slot may have generation records but no active-generation flag. No native C++ layout or new ad-hoc state file is introduced.

`PersistentPackageRegistry::uninstall()` uses the established candidate-state durability rule:

```text
copy current registry
    ↓
clear candidate active generation
    ↓
encode bounded EPR1
    ↓
fsync(temp)
    ↓
renameat()
    ↓
fsync(state directory)
    ↓
publish candidate in memory
```

Future launches are blocked only after this durable mutation succeeds.

## Running generation pins

A live `ApplicationManager::InstanceSlot` is the authoritative M1.5 generation pin.

For a running instance, the tuple

```text
ApplicationIdentity
PackageGenerationId
ContentDigest
PeerIdentity
ApplicationInstanceId
```

remains unchanged for the lifetime of that process.

Activating generation N+1 changes only future launch resolution. Existing generation N processes continue to run generation N and keep its executable target retained.

`generation_pin_count()` is an internal/package-service coordination query. It does not create a public app API or a reference-counting primitive exposed to applications.

## Launch-target retirement

App Manager retains the exact `O_PATH` executable object for every registered immutable generation.

`retire_launch_target(application, generation)` succeeds only when:

- the generation is not the current active generation; and
- no live `InstanceSlot` references that generation.

If the generation is active, retirement returns `generation_active`. If a live process still uses it, retirement returns `generation_in_use`.

After retirement succeeds, App Manager releases its retained executable descriptor. That success is the Package Service signal that the immutable generation directory may be physically removed from code storage.

Package metadata is intentionally retained even after executable-target retirement. Small historical metadata/tombstone retention is separate from large immutable code retention.

## Uninstall revocation ordering

`ApplicationManager::uninstall_application()` uses this order:

```text
1. persist PackageRegistry no-active-generation state
2. revoke each running instance from supervisor identity registry
3. request SIGTERM for each running instance
4. reap exited children through normal App Manager maintenance
5. retire generation targets only after pin count reaches zero
```

The durable package mutation comes first so a later revocation/process-control failure cannot reopen the package for a new launch.

Supervisor identity revocation happens before waiting for process exit. A process that is slow to terminate therefore loses supervisor-mediated ENML service identity immediately instead of retaining authorization until `waitpid()` completes.

M1.5 does not yet have transferable public object-capability handles for ordinary applications. When those arrive, handle-specific revocation semantics must be layered on top rather than assuming process-registry removal magically invalidates every delegated object reference.

## Principal and private-data retention

Uninstall does **not** delete `ApplicationPrincipalStore` mappings and does **not** delete the trusted per-user private-data profile.

This means an authorized same-signer reinstall for the same user receives:

- the same durable application `PrincipalId`;
- the same private-data root/profile;
- a newly selected active package generation;
- a fresh logical `ProcessId`;
- a fresh `ApplicationInstanceId`.

This is deliberate identity continuity, not a data-retention policy exposed to applications.

A future storage/key milestone must make user-requested data deletion and cryptographic erasure explicit. Package-code removal, principal tombstone retention, private user-data deletion, backup state, and key destruction are separate policy operations.

## Same textual PackageId under a different signer

Uninstall does not free the PackageId reservation. A different signer lineage still receives `package_id_collision`.

This prevents a malicious or unrelated package with the same textual identifier from inheriting:

- the previous principal;
- the previous data profile;
- the previous update lineage;
- historical generation identity.

Signer-lineage migration, if ever supported, must be an explicit verifier-approved transition rather than an uninstall/reinstall side effect.

## Source alignment

The additional reference set reinforces this milestone rather than changing its architecture:

- Symbian architecture material treats the process as a unit of trust, separates capabilities from data caging, uses client/server ownership for system resources, and places software installation behind an OS service boundary.
- Symbian smartphone material emphasizes resource-frugal, robust behavior in the presence of third-party software; M1.5 therefore adds no polling daemon, worker pool, or background lifecycle thread.
- Samsung Knox material treats software updates as a security lifecycle, includes rollback prevention, and preserves security-relevant trust state across future updates. ENML does not copy Knox mechanisms, but the principle supports retaining signer/generation history rather than treating reinstall as a clean identity reset.
- General OS references keep process execution, resource allocation, files, communication, and protection behind stable OS services instead of application-controlled Linux mechanisms.

## Verification gate

The M1.5 integration test proves:

- staged generation 2 does not affect generation-1 launches until activation;
- three running generation-1 instances create three live generation pins;
- active generation 2 cannot be retired even with zero generation-2 processes;
- generation 1 cannot be retired while any old-generation instance remains;
- activating generation 2 leaves old-generation processes unchanged;
- uninstall durably removes future launch resolution;
- uninstall immediately revokes supervisor identity for processes from both old and new generations;
- new launch attempts fail with `no_active_generation` after uninstall;
- generation targets can be retired only after processes are reaped;
- the durable application principal survives uninstall;
- same-signer generation-3 reinstall reuses that principal and retained private-data profile;
- reinstall still receives fresh process and application-instance identities;
- a second uninstall remains safe and leaves the principal mapping intact.

The package unit/persistence tests additionally prove signer ownership survives uninstall/reopen and same-signer generation numbering remains monotonic.

## Known M1 limits

The package registry still has milestone-only bounded capacities. M1.5 does not implement unbounded generation-history compaction, a final package-store garbage collector, public background registrations, storage-key destruction, backup restoration, or a full object-capability revocation graph.

Those features must preserve the lifecycle invariants above when introduced.

## Next

The next implementation milestone should move into storage/data-caging service construction rather than adding more package-manager surface area. The package/app lifecycle is now sufficient to support a real per-app private storage API without exposing Linux paths or descriptors as public ABI.
