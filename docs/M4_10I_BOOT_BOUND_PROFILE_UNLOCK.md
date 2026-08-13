# M4.10i — Persistent boot-bound profile encryption

Cookie's protected user domain must remain encrypted across shutdown, reboot, update, recovery entry, and offline storage removal. Persistence is therefore a key-release problem, not a request to leave keys resident across boot.

## Reference-derived properties

- BitLocker: bind storage-key release to trusted boot measurements and, for stronger physical-attack resistance, require pre-boot user authentication plus hardware anti-hammering. Cookie imports the properties, not TPM/PCR or Windows ABI.
- Android/GrapheneOS: separate boot-available device data from credential-encrypted user data; keep credential-encrypted profile keys unavailable until user authentication; make retry throttling depend on tamper-resistant hardware state rather than a resettable filesystem counter.
- Apple Data Protection: root long-lived data-protection keys in dedicated secure hardware and avoid exposing persistent root material to ordinary kernel/application memory where hardware supports inline encryption.

## Cookie storage classes

1. **Boot domain** — minimum data required to verify Cookie, initialize the kernel, render trusted unlock UI, provide accessibility/emergency functions, and resume a pending destruction transaction. It is device-bound and boot-measurement-bound. It MUST NOT contain private application/profile content.
2. **Credential profile domain** — the default location for user and application data. Its root is released only after trusted measured boot + successful credential gate + rollback-resistant hardware anti-hammering.
3. **Ephemeral boot domain** — per-boot state with no durable confidentiality requirement and no persistence across reboot.

Cookie deliberately avoids a broad always-unlocked user-data class. Data needed before first unlock must be individually justified and minimized.

## Release policy

A credential-encrypted profile root may be released only when all are true:

- verified/measured boot matches the root's sealed policy;
- the credential is accepted;
- the credential gate proves hardware-backed rate limiting whose failure/attempt state cannot be reset by reboot or storage rollback;
- no durable `destruction_pending` state exists for the protected domain;
- the profile is not already destroyed.

Correct password alone is insufficient. A modified OS, cloned disk image, resettable software counter, or rollback to a pre-duress filesystem snapshot cannot satisfy the policy.

## BitLocker-style hierarchy, Cookie-owned

Cookie adopts the envelope-key principle rather than direct password encryption:

`hardware device secret + measured boot + credential gate output -> profile KEK`

`profile KEK -> unwrap random profile root`

`profile root -> Storage protection root -> random object keys -> authenticated encrypted chunks`

The password/PIN never directly encrypts bulk storage and is never itself the disk key. Changing a credential re-wraps protected roots instead of rewriting all user data.

## Boot persistence

Encrypted data survives reboot as ciphertext. What persists is wrapped key material and authenticated policy metadata, not plaintext root keys. Every reboot reconstructs key authority only after the release policy succeeds.

A warm reboot, crash, or update must clear plaintext profile roots and derived keys from ordinary memory. Suspend-to-RAM is a separate threat state and may require memory-encryption support or forced key eviction for the highest protection mode.

## Metadata

File contents alone are insufficient. Cookie must protect private filenames, directory topology, sizes/attributes where practical, application indexes, thumbnails, databases, temporary files and journals. A small boot-domain metadata set may remain available only when required to reach the unlock screen and must not reveal private profile contents.

## Recovery and updates

Recovery must verify an authorized Cookie recovery image before any protected key release. Recovery is not a bypass around the credential gate. Authorized OS updates require a reviewed resealing transition so a legitimate version change does not silently teach an arbitrary current image to unseal old keys.

A recovery credential, if the product eventually supports one, is an explicit additional protector with the same audit/throttling requirements. There is no universal vendor/backdoor recovery key.

## Duress convergence

`destruction_pending` outranks unlock. Once durably committed, normal boot must resume destruction before any protected profile key can be released. Power removal must not convert destruction into an unlock opportunity.

## Production exit criteria

Cookie may claim persistent boot-bound encrypted profile storage only after real hardware provides: verified/measured boot, rollback-resistant monotonic state, non-exportable/effaceable root-key storage, hardware anti-hammering, secure randomness, and tested zeroization/key eviction. Host and QEMU implementations remain functional-development evidence, not equivalent physical-security assurance.
