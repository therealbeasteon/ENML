# M1 Status

## Completed and merged

- M1.0 package identity, signer continuity, immutable monotonic generations
- M1.1 bounded hostile package manifest analyzer and fuzz gate
- M1.2 persistent package staging and atomic activation
- M1.3 trusted App Manager generation-bound launch

## Current branch: M1.4 application principals and private-data sandbox

Implemented on `m1-4-app-principal-sandbox`:

- device-local persistent per-user `ApplicationPrincipalStore`
- explicit bounded little-endian `EPI1` principal snapshot
- monotonic non-reused application PrincipalId allocation
- mode-0600 atomic temp/fsync/rename/fsync persistence
- `O_NOFOLLOW` principal-state loading and corrupt-state rejection
- launch-target registration no longer accepts PrincipalId or sandbox authority
- separate trusted `(ApplicationIdentity, UserId)` runtime profile
- exact private-data directory retained as an authorized `O_PATH` handle
- executable retained as an `O_PATH` object and executed with `execveat(..., AT_EMPTY_PATH)`
- internal application bootstrap layout: fd3 bootstrap, fd4 executable, fd5 private-data root
- descriptor-based application Landlock policy
- private-data root writable but never granted execute permission
- unrelated filesystem roots denied even when a directory fd was opened before Landlock
- stable application PrincipalId across launches and active-generation updates
- distinct PrincipalIds for different users and signer lineages
- fresh logical ProcessId and ApplicationInstanceId for every process/launch
- native GCC, Clang and AArch64 M1 gates plus full inherited M0 regression gates

See `docs/M1_4_PRINCIPALS_SANDBOX.md` for the frozen milestone invariants.

## Next after M1.4

M1.5 should finish package update/uninstall/revocation and generation-retention semantics. Running instances must pin the immutable generation they use; uninstall must block future launches without accidentally reusing application identity; code removal, application principal history, user data and later cryptographic-key destruction must remain separate policy decisions.
