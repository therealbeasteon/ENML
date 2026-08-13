# M4.10r — Rollback-bound protected namespace snapshots

Cookie treats protected namespace metadata as security-sensitive state. A valid AEAD tag proves integrity, not freshness: an attacker controlling persistent media may replay an older valid snapshot. Namespace recovery therefore requires both authenticated encryption and independently trusted freshness evidence.

## The invariant

A namespace snapshot is authoritative only when **both** conditions hold: its AEAD authentication succeeds under the profile metadata key, and its authenticated freshness tuple `{UserId, SecurityEpoch, sequence}` is accepted against independently trusted rollback-resistant state. Either condition failing leaves the registry unchanged.

## Required bindings

Every durable namespace snapshot is bound to:

- the exact `UserId`;
- a profile-scoped metadata-encryption key purpose distinct from bulk object data;
- the current rollback-resistant `SecurityEpoch`;
- a forward-only namespace snapshot sequence;
- the bounded entry count and canonical snapshot format version.

The snapshot may become authoritative after boot only if its authenticated header matches independently trusted current user/epoch state and its sequence is not older than the trusted minimum sequence.

## Reference-derived principles

Android FBE separates file-data protection from filesystem metadata protection and protects metadata keys beneath Verified Boot / KeyMint. Cookie takes the separation-of-domains and boot-bound-key principles, not Android's fscrypt or Linux filesystem implementation.

Apple Secure Enclave designs use anti-replay state for security-critical protected state rather than treating authentication tags alone as rollback protection. Cookie takes the anti-replay principle while keeping the interface hardware-neutral.

## Fail-closed rules

- A stale or future security epoch is rejected.
- A stale sequence is rejected even when the snapshot authentication tag is valid.
- Wrong-user snapshots are rejected.
- Unknown format versions, non-zero reserved flags, malformed entries, duplicate namespace identities, and capacity overflow are rejected.
- Recovery never reconstructs object IDs from pathnames. Stable object IDs and authoritative generations come only from the authenticated snapshot.
- A snapshot is never accepted while profile destruction is pending; boot policy resolves duress state before profile Storage authority is restored.

## What M4.10r delivered

The dedicated profile Storage metadata key purpose, provider scope enforcement for that purpose, the rollback-bound snapshot header, independent user/epoch/sequence freshness validation, and negative freshness tests in the Storage CI matrix.

Canonical entry serialization, AEAD sealing of complete snapshots, restore-time validation and all-or-nothing registry replacement were M4.10s, which has since landed along with M4.10t's protected Storage cutover seam. See `docs/M4_10S_PLAN.md`, `docs/M4_10Q_PROTECTED_NAMESPACE.md` and `docs/M4_10P_PROTECTED_ATOMIC_REPLACE.md`.

**The seam is not yet wired.** `ProtectedReplaceHandler` is declared, stored on `StorageService` and constructed by nothing; durable Storage writes still take the substrate `atomic_replace` path and do not reach the encryption engine below. `docs/ROADMAP.md` Phase 1 scores this as an unmet exit criterion rather than a completed one.