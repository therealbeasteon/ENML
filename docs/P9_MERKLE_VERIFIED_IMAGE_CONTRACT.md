# P9 Merkle-Verified Immutable Image Contract

Status: PREPARATION ONLY. Implementation is gated behind the roadmap's P9 entry criteria.

## Purpose

Cookie OS needs a native integrity mechanism for immutable system/update content that does not require trusting every storage block merely because the release manifest was signed. The proposed mechanism uses a Merkle tree to reduce trust in bulk storage to a compact authenticated root while preserving Cookie's own boot/update format and capability architecture.

A Merkle tree is an integrity structure, not an encryption or authorization mechanism. Cookie must authenticate the root separately and must bind accepted roots to rollback-resistant version state.

## Security objective

Before executable or security-critical immutable content is consumed, Cookie must be able to prove that the exact block belongs to the authenticated release image identified by the active signed manifest.

The verifier must fail closed for any data, metadata, proof, geometry, root, version, or cache mismatch.

## Proposed release binding

A signed Cookie release manifest should commit to at least:

- product / hardware compatibility identifier;
- release version and rollback epoch;
- immutable image identifier;
- byte length and logical block size;
- number of data leaves;
- Merkle hash algorithm identifier;
- Merkle construction version;
- authenticated Merkle root;
- update slot / generation identity where applicable;
- policy for corruption handling and recovery selection.

The signed manifest authenticates the root. Rollback-resistant device state decides whether that authenticated release is still acceptable.

## Tree construction requirements

Cookie must not invent a hash function. Use a reviewed standardized cryptographic hash behind the project's crypto provider boundary.

Hash inputs must be unambiguous and domain-separated. A construction version should define fixed encodings similar to:

- leaf = H("COOKIE-MERKLE-LEAF" || version || image_id || leaf_index || block_length || block_bytes)
- node = H("COOKIE-MERKLE-NODE" || version || tree_level || left_hash || right_hash)

The exact byte encoding must be fixed-width or otherwise canonical. Never concatenate variable-length fields without explicit length or fixed-size encoding.

Leaf and internal-node domains must be distinct so a data block cannot be interpreted as a tree node. Image identity must be included so a valid proof from one Cookie image cannot be replayed against a different product image that happens to share block contents.

## Odd-node and geometry rules

The construction version must specify exactly how odd node counts are handled. Do not allow the verifier to accept multiple conventions for the same tree.

The manifest must define the image's logical block geometry. The verifier must reject:

- zero block size;
- unsupported block size;
- leaf counts inconsistent with image size;
- indices outside the committed leaf range;
- arithmetic overflow while calculating offsets or tree positions;
- proof depth inconsistent with the committed geometry;
- extra hashes after a root has already been derived.

## Verification API shape

The P9 implementation should expose a small verifier interface whose caller supplies:

- authenticated immutable image descriptor;
- leaf index;
- actual block bytes read from storage;
- bounded authentication path.

The verifier returns only a success/failure result plus, if useful, a compact typed corruption reason. It must not accept a caller-supplied 'trusted' root outside an already authenticated release descriptor.

Proof parsing must be allocation-bounded. No proof-controlled unbounded vectors, recursion depth, or attacker-selected memory growth should be permitted in the early boot/update trust path.

## Verified-block cache

A cache is optional optimization, never an authority source. A cache key must include enough generation information that verification under one root cannot authorize bytes after slot, release, or tree replacement.

At minimum bind cached verification to:

- immutable image identity;
- authenticated root identity or root generation;
- rollback/release generation;
- block index;
- block contents identity where needed by cache design.

Changing active release/root must invalidate or logically namespace every prior verification result.

## Boot and update integration

Suggested flow:

1. Hardware/platform trust anchor authenticates Cookie release metadata according to the verified boot chain.
2. Cookie checks rollback policy.
3. The accepted manifest provides the immutable-image Merkle root and geometry.
4. P9 storage exposes blocks plus authentication paths without being trusted to tell the verifier what root to use.
5. The verifier proves each security-critical block before it is used.
6. Corruption of executable/system metadata fails closed into the authenticated recovery/update policy.

Cookie should not silently fall back to unverified reads after a proof failure.

## Update construction

Merkle trees should also help update efficiency without weakening final-image verification. An updater may fetch only changed blocks and their metadata, but the newly assembled inactive slot is not bootable merely because individual downloads were transport-authenticated. Before activation, the resulting image must be committed to the exact authenticated release root and rollback state.

For A/B updates, root identity belongs to the slot generation. A successful proof against slot A must never authorize the same block offset in slot B without B's authenticated root.

## Package/content use

The same primitive may later protect immutable packaged resources, but package roots must use a distinct construction/product domain from system-image roots. Do not create one global tree spanning unrelated trust domains unless a later threat model proves a reason.

Mutable user data should not be forced into this immutable-tree design. Mutable authenticated storage requires different structures and crash-consistency semantics.

## Transparency / provenance separation

Append-only package/update transparency is a separate Merkle use-case. It should use an RFC-9162-style security model with inclusion and consistency proofs, separate domain separation, separate roots, and separate trust policy.

Do not reuse an immutable image tree as an append-only log or vice versa.

## Adversarial test gate

P9 Merkle verification is not complete until tests prove rejection of at least:

1. one-bit data corruption;
2. one-bit authentication-path corruption;
3. forged root;
4. proof for the wrong leaf index;
5. proof from another image/product domain;
6. proof from an older accepted root after rollback state advances;
7. truncated proof;
8. overlong proof;
9. invalid odd-node construction;
10. malformed geometry and arithmetic overflow;
11. leaf/node domain-confusion attempts;
12. stale verified-cache entry after root/slot generation changes;
13. valid bytes placed at the wrong logical block index;
14. interrupted update where metadata and data come from different release generations.

Positive tests must cover first/last leaf, odd/even leaf counts, minimum supported image, large supported image, and deterministic tree-root generation from canonical test vectors.

## Performance contract

Integrity checking must be measurable. Before release, benchmark:

- tree metadata overhead;
- boot critical-path hashes;
- random verified-read latency;
- sequential verified-read throughput;
- cache hit/miss behavior;
- memory used by proof verification;
- update-time tree construction cost.

Optimization must not create a bypass around root, index, generation, or proof validation.

## Privacy properties

Merkle verification can be local and does not require telemetry or a cloud account. Cookie must avoid sending per-block verification information externally. Release transparency, if used, should expose public software provenance rather than device-specific usage histories.

## Non-goals

Merkle trees do not by themselves provide:

- confidentiality;
- user authentication;
- capability authorization;
- signer identity;
- rollback protection;
- freshness;
- secure key storage;
- protection from malicious code that was validly included in a signed release.

Those properties come from other Cookie security layers and must remain independently enforced.

## Roadmap placement

- M7/P8: no production dependency on this primitive.
- P9: implement immutable-image verification and update/root lifecycle.
- P13: adversarial corruption/rollback/cache-replay review and fuzzing.
- P15: production release manifests and reproducible roots become release evidence.
