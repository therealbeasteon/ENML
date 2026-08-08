# M1.0 Package Identity and Immutable Generation Foundation

M1 starts by freezing the semantic distinction between a package name, a signer lineage, an application identity, and an immutable package generation before a persistent Package Service or App Manager is allowed to depend on them.

## Identity model

A textual `PackageId` is only a name. It does not establish application authority by itself. `ApplicationIdentity` is the pair:

```text
PackageId + trusted SignerLineageId
```

`SignerLineageId` is opaque input from the future trusted package verifier. M1.0 deliberately does not choose a signature algorithm or accept signer identity from untrusted package metadata. Likewise, `ContentDigest` is an opaque verified content identity; the cryptographic suite remains the verifier/security layer's responsibility.

The initial canonical PackageId spelling is bounded lowercase ASCII, segmented by dots. Segments start with a lowercase letter and may then contain lowercase letters, digits, `-`, or `_`. Filesystem paths, uppercase aliases, empty segments, leading digits, and arbitrary Unicode normalization forms are not accepted as package identity.

## Immutable generations

`PackageGenerationRecord` binds an `ApplicationIdentity`, a nonzero monotonically increasing `PackageGenerationId`, and a verified content digest. Staging a new generation never changes the active generation. Activation is a separate explicit operation.

The M1.0 in-memory registry retains old generations after activation of a newer one. It refuses to auto-evict generations when its small milestone-only retention capacity is reached because a later App Manager may still have a running process bound to an old immutable generation.

A PackageId already owned by one signer lineage rejects a different signer lineage with `package_id_collision`. This prevents a same-name package from silently inheriting the existing application's identity or data.

## Deliberately deferred

M1.0 is not yet a package installer. It does not implement signature verification, a persistent registry, package archive parsing, filesystem staging, atomic on-disk activation, permission grants, migration scripts, or app launch. Those are subsequent M1 slices and must preserve the identity/generation semantics established here.

The next slice is M1.1: a bounded package manifest/content model plus an untrusted package analyzer boundary, followed by M1.2 persistent staging/atomic active-generation state and M1.3 App Manager launch binding.
