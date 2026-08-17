# Audit 2026-08-17 — the loader path, and what the surface cannot express

An audit run after M7.16c's two increments landed, against the question the
milestone is now blocked on: **can a userland loader actually start a program
with the surface that exists?**

The answer is no, and the reason is not a missing implementation. It is a
missing *operation*. That is recorded here rather than fixed in passing, because
closing it raises the kernel call ceiling, and `abi.hpp` says in as many words
that the next call added is "worth deciding on purpose rather than in passing".

## Finding 1 — there is no way to seal a translation root *(blocking, unfixed)*

To start a program a loader must: create a space, map the program's segments
into it, and admit a thread. The first two now exist and are proven from EL0
(M7.16c). The third cannot be reached.

`thread_create` does not take an entry point, deliberately and irreversibly —
`docs/M7_12_ENTRY_BINDING.md` is the whole argument. The entry is a property of
the address space, fixed when its translation root is **sealed**, and
`Kernel::thread_admit` takes the root "from the machine layer's own lookup of
the space named by `space`, never from the caller".

**Nothing in the sixteen-call ABI seals a root.** The only three callers of
`TranslationRootSealer::seal` in the tree are in `aarch64_boot.cpp`. A space
created from EL0 therefore has no entry, can never acquire one, and no thread
can ever be admitted into it. The loader can build an address space it can never
start.

This was not visible earlier for the same reason `map`'s absence was not: nothing
consumed the surface. It becomes visible the moment something tries to use the
calls in the order a loader needs them.

### What makes it a decision rather than an omission

The obvious fix — let `thread_create` take an entry — is precisely what M7.12
refused, and the refusal should stand: a loader holds a space full of code it did
not write, and a creator-supplied entry lets it enter signed code past that
code's own initialisation while the content digest still verifies. The chain
measures *content*; the attack is about *entry*.

But the same argument, applied to a loader that *did* place the content, points
somewhere else. The entry a `.ckx` declares is part of what was signed. The
loader executing that plan is not choosing an entry; it is transcribing one. The
kernel cannot check the transcription, because `docs/M7_12_FIRST_PROGRAM.md`
refuses to put a parser for attacker-supplied images at EL1 — and that refusal
should also stand.

**The resolution that fits both, and the recommendation of this audit:** sealing
is an act of *construction authority*, not of execution authority, and the two
are already separate objects in this kernel. `TranslationRootSealer::seal` takes
the **builder**, not the space. So a `seal` call should require a right that only
the principal which built the space holds, and sealing stays one-way and
once-only. Then:

- A loader can seal a space it built, with the entry the image it placed
  declared. It is the only party that could know that entry, and it is the party
  the content was handed to.
- A principal that did *not* build a space — a pager holding it to service
  faults, the case M7.12 was written about — cannot seal it, because it never
  held construction authority. It also cannot re-seal a sealed one, because
  sealing does not happen twice.
- The kernel still parses nothing, and still never takes an entry from the party
  that will run under it.

Cost, stated: this is the seventeenth kernel call, and `abi.hpp` notes Cookie is
already two past the fourteen QNX needed for four services. The ceiling raise
should be argued in the diff that makes it, alongside whether `unmap` — the other
declared-and-unbuilt call — is landing at the same time or not.

## Finding 2 — the fault-termination path is never exercised on the machine *(coverage, unfixed)*

`COOKIE:M7.11:THREAD_TERMINATED` and `COOKIE:M7.11:SURVIVED_FAULT` are emitted by
the path that runs when a fault has **no pager to answer it**: the faulting
thread is torn down and the system continues. Neither marker appears in any boot
log, and neither is in `.github/scripts/cookie-boot-markers.txt`.

So the kernel's answer to an unanswerable fault — tearing down a thread while
other threads keep running — has never run on the machine. The boot proof's only
fault is one a pager answers. This is a gap in the proof rather than a defect in
the code, and it is the more dangerous half of the fault path: the resolvable
case leaves the system as it was, and this one changes it.

Not fixed here because it is boot-proof surgery of its own — a second faulting
thread, in a region declared with no pager — and it should not ride along with a
loader change.

## Finding 3 — `CallDescriptor::blocking` is declarative only *(low, unfixed)*

Nothing operational reads it. Its single consumer is `abi_test.cpp`, which checks
it against a hardcoded list of the two calls the test itself names — so the field
is verified against a second reading of the same intention, which the ABI test's
own header calls out as the thing differential testing exists to avoid.

This is the shape `argument_count` had before M7.14, when writing an encoder that
consumed it immediately found it had been wrong since before the kernel existed.
The declarations were checked by hand during this audit and appear correct —
`send` and `receive` block on the rendezvous, `reply` deposits and synchronises
without waiting — but "appear correct" is exactly the standing of a field nothing
depends on. **The rule already recorded applies: it becomes load-bearing the
moment something reads it, and that is the moment to check it is true.**

## Finding 4 — error codes collide across namespaces in one domain *(low, by design, recorded)*

Roughly twenty error namespaces under `ErrorDomain::kernel` each number from 1,
so `{kernel, 1}` means `invalid_capability` in one namespace and something
unrelated in another. M7.14's refusal convention carries a whole
`os::core::Error` back to EL0 specifically so the domain survives — and the
domain it preserves is not, on its own, enough to identify the error.

In practice a caller knows which call it made, which disambiguates. Recorded
rather than changed: renumbering twenty namespaces is a large mechanical change
with real risk and no caller currently harmed by it. It should be settled before
anything outside this repository consumes kernel error codes.

## Fixed in the diff that carries this document

- `docs/ROADMAP.md` said `core/osabi` encodes **8** of 16 calls. It encodes 9 as
  of M7.16c, and the milestone that made it 9 did not update the sentence — the
  same drift this document's own predecessors keep finding, committed by the
  change that introduced it.
- `docs/ROADMAP.md` said `main` was green at **30/30 checks**. It is 48/48, and
  has been for some time.
- `docs/M7_16_MAP.md` did not say what bounds the permissions a caller may ask
  for. It does now, because the answer is not "the map call checks them" — see
  below.

## Checked and found sound

Recorded so the next audit does not re-derive them.

- **`memory_right_donate` is enforced**, at `create_el0_address_space`. A first
  pass searched only `core/oskernel/src` and reported it unenforced; the
  enforcement is in `boot/`. The map/donate split is real: a pager's map-only
  capability cannot be spent on a kernel object.
- **Every marker the boot gate requires is actually emitted**, and every marker
  emitted but not gated is a refusal or diagnostic path, except the two in
  Finding 2.
- **Every function declared in `machine_aarch64.hpp` has a production caller.**
  No dead machine API.
- **`reply` does not block**, so the ABI's blocking set is accurate today.
