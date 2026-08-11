# M7.4c - Anonymous attestation, and a cryptographic API that stays small

`docs/ACHIEVEMENTS.md` scopes this as "signing the boot state without minting a
device identifier, which is the ring-signature and zero-knowledge question." The
reference library turns out to answer it more directly than expected, and the
answer is unusually kind to a phone.

## The problem attestation creates

M5.0 built `BootStateV1` and M5.5 made it record what the platform's root of
trust actually provides. It is deliberately **not** self-authenticating: it is
trusted because trusted early boot produces it and it crosses no untrusted
boundary. Attestation is what happens when it does cross one - when a bank, an
employer or a messaging service wants evidence the device is running verified
Cookie.

The obvious construction is a per-device key signing the boot state. That works,
and it hands every verifier a permanent, unforgeable device identifier. For an
operating system whose first stated priority is security by default and whose
threat model includes the people who make phones, minting a global tracking
handle in order to prove integrity is not a trade worth making silently.

## What the references settle

**Ring signatures give anonymity without a manager.** A signer proves membership
of a set without revealing which member. `2022-1743` matters specifically because
it constructs ring signatures with user-controlled linkability that "require no
group manager and can be instantiated in a completely decentralized manner." A
group manager is a party who can de-anonymize on demand; on a phone that party
would be the vendor, which is precisely the entity Cookie's threat model declines
to trust. Ring rather than group is therefore not a performance choice.

**The definitions matter more than the construction.** `ring-signatures-stronger-definitions`
(Bender, Katz, Morselli) exists because the natural definitions are too weak, and
strengthens them to anonymity against *full key exposure* and unforgeability
against *insider corruption*. For a fleet of phones this is not academic
hair-splitting: an adversary will eventually extract a key from some device. A
scheme secure only under the weak definitions would lose anonymity for everyone
at the first extracted key. ENML requires the strong definitions, and records the
requirement here because it is the kind of thing a later implementer picking a
library would not know to check.

**Traceability is the abuse bound, and it needs no manager either.**
`2021-1054` (One-time Traceable Ring Signatures) is the key result: a member may
sign anonymously *at most one message per tag*, and "if a party signs two
different messages for the same tag, it will be de-anonymized." That converts the
obvious objection to anonymous attestation - a compromised device can attest
forever and nobody can revoke it - into a self-limiting problem. Attest once per
epoch and you stay anonymous; attest twice and you have identified yourself. No
authority is involved in either outcome.

**And it is astonishingly cheap.** The same paper's construction "only requires a
few hash evaluations", signs in under a second for a ring of 2^10 signers, is
"post-quantum resistant, as it only requires hash evaluations", and is "extremely
simple, as it requires only a black-box access to a generic hash function... no
other cryptographic operation is involved."

That paragraph is the whole reason this milestone is worth doing now rather than
later. A hash-only primitive is one a small kernel can carry, one whose failure
modes are understood, one that does not drag in a bignum library, and one that
does not need replacing when post-quantum standards land. For a project measuring
success in how much code has to be trusted, a scheme that needs a hash function
and nothing else is a different class of object from one that needs elliptic
curves and pairings.

**Signature size is the other cost, and there is a mode for it.** `2022-1730`
(Merkle Tree Ladder mode) reports NIST PQC signature sizes of 666 to 49,856 bytes
and condenses them to 248-472 bytes for a verifier processing a series. That is
not needed for attestation itself under a hash-based ring scheme, but it is
directly relevant to the other place Cookie will carry PQC signatures - package
and update signing, where a device fetches many signatures over time and the size
is paid on a metered radio.

## What ENML derives

**Attestation is per-epoch and per-verifier, and the OS refuses what the
primitive would punish.** The scheme de-anonymizes a device that signs twice
under one tag. That is a *precondition*, and preconditions enforced by
consequences are preconditions that get violated. So the policy layer refuses the
second attestation rather than letting the primitive punish it. This is the
inversion worth stating: the cryptography defines what must not happen, and the
operating system's job is to make it not happen - not to discover afterwards that
it did.

**Linkability belongs to the user, not the verifier.** `2022-1743`'s
user-controlled linkability is the right shape for a phone. Some verifiers
legitimately need continuity - a bank wants to know this is the same device as
last week. Most do not. So linkage is a per-verifier choice the user makes, it is
recorded in the grant, and an attestation that reveals continuity says so rather
than doing it quietly. A verifier never gets to decide.

**The ring is a design decision with a privacy budget.** A ring of one is a device
identifier. A ring of every Cookie device ever made is anonymous and proves
almost nothing. What the ring should be - a hardware model, a production batch, a
firmware version - is a trade between the anonymity set and the specificity of
the claim, and it is not settled here. It is recorded as the decision that
determines whether any of this delivers privacy in practice.

**Zero-knowledge proving stays off the device for now.** `2023-905` (zkSaaS)
exists because SNARK proving is expensive enough that delegating it is a research
area. A phone generating SNARKs on battery to prove boot state is the wrong shape
when a hash-based ring signature answers the same question. ZK earns its place
when the claim becomes richer than membership - proving a *property* of the boot
state without revealing the state - and that is a later question, recorded rather
than started.

## The cryptographic API, and why it stays small

The requirement is that this be lightweight while being very secure, and those
pull in opposite directions only if the API is built around primitives.

**Expose purposes, not primitives.** A caller asks to protect something for a
scope, or to attest a state to a verifier for an epoch. It never selects an
algorithm, a mode, a nonce or a curve. ENML already does this and it has already
paid: M5.0 made AEAD nonces provider-owned *by invariant* - a caller cannot
influence the IV, no nonce repeats under a key, and a test asserts all three,
which are exactly the properties whose absence produced CVE-2021-25444. An API
that let callers pass a nonce would have had that bug available to every caller.

**One provider boundary, and the policy above it.** M2.4 put the AEAD primitive
behind a provider and kept the key hierarchy, envelope format and rotation policy
above. The same split applies here: the ring signature is a provider, the epoch
discipline and linkage policy are ENML's. This is what lets the primitive be
replaced when the post-quantum picture changes without touching a line of policy,
and it is why the provider interface is worth more than the provider.

**Refuse rather than degrade.** M5.5 already refuses two claims the platform
cannot back - a closed verified device with no immutable first stage, and a
rollback claim with no monotonic counter. Attestation inherits that discipline: a
claim the platform cannot support is refused, never weakened into something that
sounds similar. A format that can express a claim it cannot back forces a lie.

**Hashing is the one primitive worth having everywhere.** `nist.fips.180-4`
covers SHA-2, which the tree already uses for boot measurement and package
digests. The one-time traceable ring scheme needs a hash and nothing else. Merkle
constructions need a hash and nothing else. A cryptographic surface where the
mandatory primitive count is *one* is the smallest honest answer to
"lightweight", and it is reachable here rather than aspirational.

## What is implemented at this milestone

The **epoch and linkage policy** - the part that is ENML's own and that can be
tested with no cryptography present. It refuses a second unlinkable attestation
in the same epoch to the same verifier, because that is the operation that would
de-anonymize the device; it permits repetition where the user has chosen linkage,
and records on the grant that continuity is revealed.

Deliberately not implemented: the ring signature itself, which is a provider; ring
membership, which is the unsettled design decision above; and any binding to
`BootStateV1`, which belongs to the caller that holds one - a policy object that
reached into the boot state would be two state machines that know about each
other, which this project has consistently refused.

## Not claimed

- No cryptography is present. This milestone is policy, and a policy that assumes
  a scheme nobody has selected is a policy that may not survive selection.
- The strong security definitions are stated as a requirement, not verified
  against any implementation.
- Ring membership, and therefore the actual anonymity set, is undecided. Until it
  is, no privacy claim should be made from this document.
- Nothing here addresses a device whose keys were extracted while it was still
  trusted. Traceability bounds how often it can attest; it does not detect it.
