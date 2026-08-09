# M2 Status

## M2.0 — private storage foundation

Current branch: `m2-0-private-storage`

Implemented:

- bounded fixed-capacity UTF-8 `RelativePath`
- relative-path rejection for absolute, empty, dot/dot-dot, NUL, backslash and malformed UTF-8 forms
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
- full inherited M0/M1 regression coverage

See `docs/M2_0_PRIVATE_STORAGE.md`.

## Next

M2.1: Storage Service + OSIDL/object-handle boundary. Derive private-root authority from trusted `RequestContext` identity, then return transferable typed file/directory objects with explicit rights reduction. Do not expose raw Linux paths, uid/gid, fd numbers, or caller-supplied PrincipalId as application authority.
