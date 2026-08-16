# Reference notes — cache side channels on ARM phones, and what `sealed` does not cover

**Source read:** Bo Li and Bo Jiang, *Cache Attack on AES for Android Smartphone*
(Beihang University; ACM, DOI 10.1145/3199478.3199488). Abstract, introduction
and related-work sections read directly; the statistical method and full results
were not, and nothing below depends on them.

**Why it was read:** `docs/M7_11_FAULT_PRIVACY.md` introduces a `sealed`
disclosure class for "the memory a process puts key material, plaintext and any
secret-dependent index in", and closes with the claim that `sealed` takes
observability "to zero for the memory that cannot afford even that". That is a
strong claim about key material on a phone, and it deserved a check against what
is actually known about attacking key material on a phone.

## What the paper establishes

- A Prime+Probe cache attack recovering AES keys **on Android smartphones** is
  practical, not theoretical. The authors state it plainly as their result and
  say countermeasures are needed.
- ARM is not Intel, and that mattered for the attacker rather than the defender:
  instruction set, cache organisation and replacement strategy all differ, which
  is *why* effective ARM attacks arrived later — not why they do not exist.
- It cites Lipp et al. for cross-core attacks on ARM **without root**, and notes
  cache channels have been used to infer keystrokes and build a keylogger, not
  only to recover cryptographic keys.

## What that means for Cookie, concretely

**It confirms the threat `sealed` was invented for.** Cookie targets exactly this
platform class - ARM phones - and the memory `sealed` is meant to protect is
exactly what this attack recovers. The disclosure class is aimed at a real
target, not a hypothetical one.

**And it bounds what `sealed` can honestly claim.** A cache attack observes
access patterns at cache-line resolution through timing. It does not use page
faults, needs no pager, and is unaffected by anything `FaultRegionTable` decides.
So `sealed` reduces *fault-derived* observation to zero and leaves
microarchitectural observation exactly where it was.

That distinction was missing from the document. "Takes it to zero" reads as
"sealed memory is unobservable", which is false on the hardware Cookie runs on.
`docs/M7_11_FAULT_PRIVACY.md` now says which channel it closes.

This is the correction worth having: a security document that overstates its
coverage is worse than one that claims less, because the overstatement is what a
future design leans on when deciding where to put a key.

## What this does not license

It does not license adding cache partitioning, scheduler colouring or flush-on-
switch to the kernel now. Cookie is single-CPU today, so cross-core Prime+Probe
is not reachable on the current target, and building a mitigation for a threat
the system cannot yet experience would be speculative work in the most expensive
place - `core` is 1,865 semicolons against a 4,529 target, and the room is there
to be spent on things that are demonstrably needed.

What it does establish is that **the day Cookie schedules two mutually
untrusting threads on cores sharing a cache, `sealed` needs a second meaning** -
something about co-residency, not just about faults. That is a design question
for whichever milestone introduces SMP, and it should arrive with the SMP work
rather than be retrofitted after it.

`docs/M6_2_TIME_PROTECTION.md` already owns the timing axis; this note is the
memory-side half of the same boundary.
