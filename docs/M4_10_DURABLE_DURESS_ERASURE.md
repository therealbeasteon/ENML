# M4.10g — durable duress and brute-force erasure

## Implementation authority

**References teach principles. EMNL determines implementation. External systems are not the design specification.**

This slice continues the already-implemented M4.10 rule that an explicit duress credential and reaching the owner-selected failed-unlock threshold are two triggers for the same duress response. It does not add a second wipe mechanism.

The security objective is stronger than deleting directory entries or issuing ordinary filesystem deletes: after the destructive transition, recovery of protected user data must be infeasible even when an attacker later acquires the persistent media. Cookie will only make that claim on a platform and storage design that can actually support it.

## Existing trigger semantics

M4.10e already defines repeated failed unlock attempts as duress. The threshold is an explicit owner choice within the existing bounded range. When the threshold is reached, the observable response remains the same class as the explicit duress credential so the trigger is not revealed by a different lock-screen result.

The failed-attempt state is durable across ordinary service/process restart. That is necessary but not sufficient for the final security claim: persistent state stored only in rewritable ordinary storage can still be rolled back by a sufficiently capable offline attacker. Production enforcement therefore remains tied to the `MonotonicSecurityState`/verified-boot work rather than treating a durable file as an anti-rollback primitive.

## One irreversible destruction state machine

The eventual product transition is:

`active -> destruction_pending -> destroyed`

There is no transition from `destruction_pending` or `destroyed` back to `active`.

`destruction_pending` must be committed to trusted rollback-resistant state **before** the first irreversible key operation. Power loss at any later instruction therefore resumes destruction rather than restoring normal access. The device must not require a reboot to complete a duress response, and a late failure must never turn an already-started erasure back into a data-preserving state.

Repeated guessing and an explicit duress credential enter exactly this same state machine.

## M4.10g KeyHierarchy mechanism

`KeyHierarchy::destroy_profile(UserId)` is the first lower-level mechanism added for that state machine.

It destroys every application root beneath the trusted profile first and destroys the profile root only after all descendants have succeeded. A successfully destroyed child slot is cleared immediately. If the provider fails on a later child, the operation is retryable: already-destroyed authority does not reappear, the parent remains only so destruction can finish, and unrelated profiles are untouched.

The provider also reports a narrow `RootErasureAssurance`:

- `logical_only` — the default for software/unknown providers. Root deletion succeeded logically, but no forensic claim follows.
- `effaceable_key_storage` — the provider explicitly states that the root lives in storage whose key material can be securely effaced.

This assurance describes the **provider root only**. It is deliberately not named `forensic_erasure` because a key can be perfectly destroyed while plaintext copies of the user's data remain elsewhere.

## Why deleting keys rather than overwriting files

Modern flash storage, remapping and wear-leveling make host-visible overwrite an unreliable primitive for proving that every historical physical copy has been overwritten. Cookie therefore follows a cryptographic-erasure architecture: user data is encrypted beneath a small protection hierarchy and the destructive operation sanitizes the keys required to recover that data.

This is only valid if the target data was encrypted under those keys in the first place and no alternate recovery copy bypasses the hierarchy.

## The current blocker: Storage is not yet whole-profile encrypted

Cookie's current Storage Service provides descriptor-rooted confinement, typed object capabilities, quotas, revocation and crash-resistant updates. It does **not yet** provide complete encrypted-at-rest coverage for every durable byte belonging to a user profile.

Therefore M4.10g does **not** claim that the current system can make all user files forensically unrecoverable. Destroying Key Service application/profile roots today would make those protected key objects unavailable, but it would not cryptographically erase plaintext private-storage files that were never encrypted beneath that profile root.

The next destructive-security slice must add profile-scoped encrypted storage rather than hiding this gap behind wording.

## Required profile encrypted-storage shape

The future Storage encryption layer must preserve existing Storage authority and path-confinement semantics while adding a separate data-protection hierarchy:

1. Every protected profile receives an internal profile data-protection root below trusted system authority.
2. File/content keys are generated or wrapped beneath that profile domain; applications never receive raw long-lived storage keys.
3. Data and security-relevant metadata are authenticated, not merely encrypted.
4. Nonces/IVs are generated by the trusted crypto provider and never chosen by applications.
5. Atomic replacement remains crash consistent: neither power loss nor service restart may publish unauthenticated or partially re-keyed state.
6. Caches, thumbnails, indexes, journals, temporary files and other persistent derivatives must either be encrypted beneath the same destructible domain or explicitly classified as non-sensitive.
7. Swap/paging must not create plaintext copies. Cookie currently plans no paging to storage, which simplifies this property.
8. Backup/recovery/update paths must not retain an older key that silently defeats local destruction.
9. In-memory derived keys and plaintext scratch buffers must be bounded and securely cleared when their lifetime ends.
10. The host/OpenSSL development provider remains `logical_only`; tests may exercise semantics but cannot manufacture hardware forensic assurance.

Cookie will use reviewed standardized cryptography through the provider boundary. This milestone does not invent a cipher or storage mode.

## When Cookie may claim forensic/media sanitization

A product build may describe the duress result as cryptographic/media sanitization only when **all** of the following are true:

- all protected target data and persistent derivatives are encrypted beneath the destructible profile domain;
- the provider reports `effaceable_key_storage` based on a reviewed platform mechanism rather than configuration text;
- the destruction-pending/destroyed state is rollback-resistant and bound into verified boot/recovery policy;
- no recovery, backup, factory, debug or update path can restore the destroyed root or an equivalent plaintext copy;
- destruction completes locally without depending on network connectivity or a future reboot;
- power-loss tests at every state transition prove that restart resumes destruction;
- application/root capabilities are revoked before destroyed data can be reopened;
- memory-lifetime review covers derived keys and plaintext buffers;
- platform-specific sanitization validation demonstrates that recovery of the target data is infeasible at the declared attacker capability.

Until those hold, the UI and documentation must say only what is true: destruction was requested/completed at the logical key hierarchy, not that a forensic laboratory cannot recover all user data.

## Reference guidance

Current NIST SP 800-88 Revision 2 treats cryptographic erase as sanitizing the media-encryption key(s) protecting encrypted target data so recovery of the decrypted data becomes infeasible. That definition reinforces the dependency above: key destruction is meaningful only when the target data is actually protected by those keys and the sanitization implementation can be trusted.

Android's file-based-encryption architecture is useful evidence for separating credential-encrypted user storage, device-available storage and protected key wrapping. Cookie does not import Android's `vold`, filesystem layout, KeyMint ABI or Direct Boot model.

Apple's Data Protection/effaceable-storage documentation is useful evidence for a hierarchy in which per-file/metadata keys ultimately depend on a small securely erasable wrapping key, enabling fast cryptographic wipe. Cookie does not import Apple's Secure Enclave interfaces, APFS keybag formats or platform ABI.

The supplied BitLocker/FIPS/key-storage references continue to guide separation of bulk encryption, key wrapping, protected roots, lifecycle, zeroization and recovery. Historical algorithms, Windows boot architecture and compliance claims are not inherited.

## Next development slices

1. Wire the durable duress destruction-pending state to a trusted Key Service control operation rather than leaving `destroy_protected_domain` as a shell-only directive.
2. Add the profile encrypted-storage root and authenticated storage format while retaining M2 descriptor-rooted confinement and atomic replace.
3. Integrate destruction state with the production monotonic/verified-boot source so power-cycle or offline rollback cannot reset the failed-attempt threshold or resurrect a destroyed domain.
4. Add power-cut/restart tests spanning `active -> destruction_pending -> destroyed` and Key/Storage service restart.
5. Only after those pass on a target with real effaceable key storage should the product make a forensic/media-sanitization claim.
