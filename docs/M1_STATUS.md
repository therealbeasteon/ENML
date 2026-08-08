# M1 Status

## Completed and merged

- M1.0 package identity, signer continuity, immutable monotonic generations
- M1.1 bounded hostile package manifest analyzer and fuzz gate
- M1.2 persistent package staging and atomic activation

## Current branch: M1.3 App Manager

Implemented on `m1-3-app-manager`:

- `ApplicationInstanceId`
- bounded application bootstrap protocol
- trusted launch-target registration from authorized directory handles
- segment-by-segment `O_NOFOLLOW` executable opening
- descriptor-based `execveat(..., AT_EMPTY_PATH)` launch
- fixed application environment and M0 sandbox baseline before exec
- supervisor-issued `PeerIdentity` before app bootstrap/READY
- active-generation resolution at launch time
- immutable running-instance generation binding
- cross-generation principal-continuity enforcement
- native x86-64 / Clang / AArch64 integration gate

## Next after M1.3

M1.4 should allocate/persist per-user application principals and introduce per-application sandbox profiles/data roots. It must preserve M1.3's no-path launch API and generation binding rather than replacing them.
