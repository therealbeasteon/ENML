# M1 Status

## Completed and merged

- M1.0 package identity, signer continuity, immutable monotonic generations
- M1.1 bounded hostile package manifest analyzer and fuzz gate
- M1.2 persistent package staging and atomic activation
- M1.3 trusted App Manager generation-bound launch
- M1.4 durable per-user application principals and private-data sandbox

## Current branch: M1.5 update/uninstall/revocation and generation retention

Implemented on `m1-5-update-uninstall`:

- durable package uninstall represented by an EPR1 application with no active generation
- PackageId/signer ownership retained across uninstall
- historical generation metadata retained across uninstall
- same-signer reinstall continues monotonic generation numbering
- App Manager live instance table used as the authoritative generation-pin set
- active generation cannot be retired while it remains a future launch target
- inactive generation cannot be retired while any live process still uses it
- explicit launch-target retirement releases the retained executable object only after pin count reaches zero
- uninstall commits durable no-active state before process revocation
- uninstall immediately revokes supervisor identity for every running instance of the application
- uninstall requests process termination and reuses normal App Manager reap/revocation maintenance
- application PrincipalId and trusted private-data profile survive uninstall
- same-signer generation-3 reinstall reuses the durable per-user PrincipalId and data profile
- reinstall still receives a fresh logical ProcessId and ApplicationInstanceId
- package/unit/persistence tests cover signer-tombstone continuity across restart
- native GCC, Clang and AArch64 M1 gates plus the full inherited M0 regression suite

See `docs/M1_5_UPDATE_UNINSTALL.md` for the milestone invariants.

## M1 completion boundary

M1 now provides a coherent package/application lifecycle substrate:

```text
hostile package metadata
        ↓ bounded analyzer
signer-bound ApplicationIdentity
        ↓
immutable staged generations
        ↓ atomic activation
trusted package-based App Manager launch
        ↓
durable per-user application PrincipalId
        ↓
private-data sandbox profile
        ↓
update generation pinning
        ↓
durable uninstall + runtime identity revocation
        ↓
safe executable-generation retirement
```

Package code state, durable application identity, private user data, and future cryptographic keys are intentionally separate resources.

## Next after M1.5

The next implementation track should begin the storage/data-caging service layer. It should build on the existing fd-rooted private-data profile while replacing the internal bootstrap fd with stable typed storage APIs/handles. Public applications must not gain raw Linux paths or descriptor numbers as ABI.
