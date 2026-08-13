# M6.2 - Time protection: the OS half

Timing attacks work by sharing a resource. Recovering an AES key from a phone by
watching cache evictions needs no privileges at all - it needs only that the
attacker and the victim contend for the same cache sets. Constant-time code
narrows one path to one secret; it does nothing about the sharing itself, and it
cannot, because sharing is not a property of a function.

Removing the sharing means partitioning a micro-architectural resource, and that
is necessarily an operating system job. The platform supplies a mechanism; the
OS decides who gets it, refuses when it cannot be given, and takes it back. The
reference design names three responsibilities, and the third is a security bug
rather than a feature:

1. grant partitioning to the principals that need it,
2. handle an acquisition that cannot be satisfied,
3. reclaim partitions held by processes that were killed.

`PartitionLedger` is those three. It is deliberately platform-independent
bookkeeping - what a "unit" is, a cache way or a colour or a TLB entry, is the
platform port's business, and the accounting rules do not change with the
answer.

## The two rules that are security properties

**A reservation may never consume the shared remainder.** A principal that
reserved every way of a cache would not be isolating itself; it would be
evicting everything else permanently. A partitioning mechanism is a bounded
resource, and bounded resources handed out on request are denial-of-service
surfaces. The floor is enforced at construction - a ledger with a zero floor
cannot be built - and at every reservation.

**A failed reservation is an error the caller must handle.** It is never
silently downgraded to running shared, and never partially granted. Code that
believes it is partitioned and is not will make exactly the assumptions the
partition was supposed to justify. `reserve` refuses rather than granting fewer
units, and distinguishes "not enough free right now" from "more than could ever
be granted", so a caller can tell whether waiting could help.

## Reclamation

`reclaim` is the path a supervisor takes when a process dies. It deliberately
does not return a `Result`: a supervisor tearing down a dead process must not
have an error to ignore on this path, and "it held nothing" is a normal answer
rather than a fault. It is idempotent, and after it the principal holds nothing.

The vacated table slot is cleared rather than left holding a stale principal and
unit count. A reclaimed reservation must not be recoverable by reading past the
live range.

There is no implicit top-up either: a second reservation by a principal that
already holds one is an error, so a caller cannot accumulate the resource by
repeating a call whose failure it never checked.

## Capabilities, and honest absence

`TimeProtectionCapability` records what the platform actually provides -
partitioned cache, line locking, partitioned TLB, flush on context switch,
deterministic arithmetic. As with the boot root-of-trust set and device DMA
confinement, absence is a fact rather than a failure: most platforms provide
none of this, and a hardware-neutral OS says so rather than assuming otherwise.

A ledger with capacity but no capability is refused. Handing out reservations
that partition nothing is worse than refusing, because the holder would believe
it was isolated.

## What this is not

**This does not program hardware.** It is the accounting, the policy and the
reclamation - the part that is ours regardless of platform. The mechanism is a
platform port and does not exist yet.

**This is not a claim that ENML has time protection.** Until a port drives real
partitioning, no timing property is established. `docs/SECURITY_REVIEW.md`
records that distinction, and it should stay recorded until a port closes it.

## Gates

`m6-time` runs under GCC, Clang, sanitizers and native AArch64.
`time_protection_fuzz` runs in the per-PR smoke set and the nightly. It is a
state-machine fuzzer rather than a decoder fuzzer, because the failure mode here
is an ordering rather than a malformed input - a reserve racing a reclaim, a
release of something never held, a reclamation that leaves accounting behind.
After every operation it asserts that reserved units equal the sum of what
holders hold, that the shared remainder is intact, that `available()` is exact
and never wraps, and that the unset principal holds nothing.
