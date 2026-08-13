# M4.10i — Boot-bound persistent profile encryption

Status: implementation in progress; security contract frozen before provider wiring.

## Threat model

Cookie assumes a physically present attacker may remove/copy storage, reboot repeatedly, attempt rollback to an older filesystem image, replace the OS/boot chain, and perform offline credential guessing. A correct user credential by itself is therefore insufficient authority to release a protected profile root.

## Required release factors

A production profile root may be released only when all of the following hold:

1. the running boot chain matches the measurement to which the protector was sealed;
2. the user credential was accepted by a hardware-backed credential gate;
3. the credential gate's retry state is rollback-resistant and cannot be reset by rebooting or restoring storage;
4. the persistent protector record is bound to the exact UserId, boot measurement, credential-gate slot, security epoch, and protector generation;
5. no durable destruction-pending state exists for the profile.

A software-only password KDF is not sufficient for a production protected profile. It may increase the cost of a guess, but it must not become the authoritative retry counter or an offline password verifier.

## Persistent protector record

`ProfileProtectorRecordV1` is a canonical little-endian record. Its 80-byte authenticated header contains:

- magic/version/header size;
- durable UserId;
- monotonic security epoch;
- SHA-256 boot measurement digest;
- durable hardware credential-gate slot identifier;
- protector generation;
- opaque provider blob length;
- reserved zero field.

The provider blob is opaque to Cookie core and MUST NOT be plaintext long-lived key material. A production provider may store a hardware-sealed/wrapped key object or a durable secure-object locator. The provider must authenticate the exact canonical header as binding metadata.

Ciphertext and opaque wrapped references survive reboot. Plaintext profile roots do not.

## Key hierarchy

The target hierarchy is:

`hardware device root / boot measurement / credential gate`

→ profile key-encryption authorization

→ random profile root

→ Storage protection root

→ random object keys

→ authenticated encrypted chunks

The user's PIN/password is an authorization factor, not the bulk-encryption key. Changing a credential rebinds/rewraps the random profile root instead of re-encrypting every user object.

## Boot lifecycle

Cold boot proceeds in this order:

1. immutable/platform root verifies the next boot stage;
2. Cookie boot chain is verified and measured;
3. monotonic security/destruction state is read before normal profile unlock;
4. if destruction is pending, normal profile-key release is forbidden and destruction resumes;
5. otherwise the trusted pre-unlock UI may request a credential;
6. the hardware credential gate verifies it and advances durable retry state as required;
7. the boot sealing policy verifies the current measurement;
8. the protector record epoch/generation is checked against trusted monotonic state;
9. only then may the provider restore/release the profile-root reference;
10. the profile Storage domain becomes available.

Rebooting never turns an unlocked profile into an automatically unlocked profile merely because ciphertext and wrapped keys persisted.

## Storage classes

Cookie keeps pre-unlock state minimal:

- **Boot domain:** only verified-boot state, the trusted lock-screen prerequisites, accessibility/emergency prerequisites, hardware/protector metadata, and state required to resume destruction.
- **Credential profile domain:** default home for user/application data; unavailable before successful profile unlock.
- **Ephemeral boot domain:** temporary state intentionally discarded on reboot.

Anything promoted to the Boot domain requires a written justification because it is available before the user credential has released the private profile domain.

## Duress and brute-force erasure

Repeated failed unlock attempts may trigger the same duress destruction transaction as an explicit duress credential. The threshold policy is owner-selected, but its authoritative failure count must live behind rollback-resistant hardware/monotonic state.

`destruction_pending` outranks normal unlock. Once committed, no subsequent reboot, correct password, recovery environment, or older filesystem snapshot may release the protected profile root. The system resumes cryptographic destruction until the profile root and descendants are gone.

## Update and recovery rules

A legitimate signed Cookie update may migrate a protector from one approved boot measurement to another only through a reviewed, authenticated transition while the old trusted system still has authority. An arbitrary new measurement must never be allowed to silently reseal itself.

Recovery is not a universal vendor escrow key. Any future recovery protector must be explicitly owner-authorized, separately auditable, and subject to the same rollback/destruction policy. Recovery must not provide a path to bypass a committed duress wipe.

## Research-derived principles

- BitLocker: bind disk-key release to trusted platform/boot state; preboot PIN plus hardware anti-hammering is stronger than a password-only protector.
- Android FBE: separate pre-unlock/device-available data from credential-encrypted user data; use a high-entropy synthetic/root secret instead of the low-entropy lock credential as the data key; hardware-enforced rate limiting is the primary defense against credential guessing.
- Android metadata encryption: file contents alone are insufficient; filesystem metadata can reveal significant private structure.
- Apple Data Protection: use wrapped hierarchical class/file keys rooted in secure hardware and anti-replay state; erasure should target small root authority rather than depend on overwriting every physical block.
- GrapheneOS/Weaver-style security: credential retry state and secure deletion belong in a secure-element/TEE class boundary, not in a reboot-resettable userspace counter.

These are security properties and failure modes, not imported vendor ABIs, filesystems, TPM PCR layouts, KeyMint interfaces, keybag formats, or process topology.

## Exit criteria

M4.10i is not complete until:

- the protector format/parser passes GCC, Clang, ASan/UBSan and native AArch64 gates;
- a production-facing provider interface can persist/restore a profile root without exporting raw root bytes;
- provider restore authenticates the complete protector binding header;
- monotonic epoch/generation rollback is rejected;
- altered boot measurements cannot release the root;
- reboot cannot reset credential retry state;
- `destruction_pending` prevents release before any normal unlock path;
- update migration is explicit rather than automatic resealing;
- recovery cannot resurrect a destroyed profile;
- plaintext profile roots are zeroized on lock/reboot/destruction boundaries and are never serialized.
