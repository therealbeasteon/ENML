# M4.10r — Rollback-bound protected namespace snapshots

Cookie treats protected namespace metadata as security-sensitive state. A valid AEAD tag proves integrity, not freshness: an attacker controlling persistent media may replay an older valid snapshot. Namespace recovery therefore requires both authenticated encryption and independently trusted freshness evidence.

## Required bindings

Every durable namespace snapshot is bound to:

- the exact `UserId`;
- a profile-scoped metadata-encryption key purpose distinct from bulk object data;
- the current rollback-resistant `SecurityEpoch`;
- a forward-only namespace snapshot sequence;
- the bounded entry count and canonical snapshot format version.

The snapshot may become authoritative after boot only if its authenticated header matches independently trusted current user/epoch state and its sequence is not older than the trusted minimum sequence.

## Reference-derived principles

Android FBE separates file-data protection from filesystem metadata protection and protects metadata keys beneath Verified Boot / KeyMint. Cookie takes the separation-of-domains principle, not Android's fscrypt or Linux filesystem implementation.

Apple Secure Enclave designs use anti-replay state for security-critical protected state rather than treating authentication tags alone as rollback protection. Cookie takes the anti-replay principle while keeping the interface hardware-neutral.

## Fail-closed rules

- A stale or future security epoch is rejected.
- A stale sequence is rejected even when the snapshot authentication tag is valid.
- Wrong-user snapshots are rejected.
- Unknown format versions, non-zero reserved flags, malformed entries, duplicate namespace identities, and capacity overflow are rejected.
- Recovery never reconstructs object IDs from pathnames. Stable object IDs and authoritative generations come only from the authenticated snapshot.
- A snapshot is never accepted while profile destruction is pending; boot policy resolves duress state before profile Storage authority is restored.

## Next slice

M4.10s serializes bounded namespace entries canonically, seals the snapshot under the profile metadata key, and restores the registry only after AEAD verification plus freshness validation.