# M7.12 — `.ckx` and `.cookie`

Cookie's executable image is **`.ckx`** and its application package is
**`.cookie`**.

The first draft of this document opened by comparing them to `.apk` and `.exe`,
and described `.ckx` as a header, a table of segments, and their bytes — offset,
length, virtual address, permissions. That is ELF's program header table with
the fields renamed. It was thrown away, and this is the record of why, because
the reason is the design.

## A `.ckx` is not loaded. It is constructed.

**Cookie has no `load` operation and never will.** Look at the system call
surface `docs/abi.hpp` fixes: create an address space, donate pages to it, map,
seal, admit a thread. There is no call that takes a file. `docs/M7_11_MEMORY.md`
made the kernel allocator-free, so a space is not filled *for* a caller — it is
*built by* one, out of memory that caller already holds authority over.

A format built around file offsets describes an operation this system does not
have. So `.ckx` describes the thing Cookie actually does: **a plan for an
address space**, in the same vocabulary the kernel already uses — regions,
permissions, disclosure classes, an entry, an authority ceiling.

There are no file offsets in it at all.

## What follows from that, and could not follow from a file layout

### 1. Content is named, not located

A region does not say *where its bytes are*. It says **what its content is**: a
digest, or `anonymous` for memory that is defined to start as zeros.

Three things fall out, and none of them are available to a format that points
into a file:

- **The bytes can come from anywhere.** A package, a cache, a peer, a pager
  answering a fault. The image does not know and does not need to. Cookie's
  fault path already asks a userland pager for backing; a region named by digest
  is a question that pager can answer from whatever it has.
- **Sharing becomes a consequence of identity.** Two regions in two different
  applications with the same digest hold the same content — provably, by
  checking, not because someone declared the same library name or the same path.
  Every other system decides sharing by *naming* and then has to be trusted
  about it. Here one physical copy can back both mappings and the reason is
  arithmetic.
- **The image is position-independent in the file**, so a `.cookie` can lay its
  contents out however it likes, and re-laying them out changes no `.ckx`.

### 2. The cost of building the space is computed, not declared

Because the kernel has no allocator, **the caller supplies every page** — the
translation tables included. A loader that discovers the cost as it goes runs
out halfway and leaves a half-built space; one that knows it up front refuses
before it starts. That is the difference between an error a caller can act on
and a mess it has to clean up, and it is the shape `docs/M7_11_MEMORY.md` chose
deliberately when it made exhaustion a caller-fixable answer.

`aarch64_construction_cost` derives that budget from the region table.
**Derived, not stored** — and that is the security content, not an
implementation detail. A declared cost is a number an image can lie about, and
the lie would be discovered exactly as the exhaustion the field existed to
prevent. A computed one cannot disagree with the plan because it *is* the plan.

It deliberately over-counts when two regions share a table: a budget that is
sometimes too large costs a page that reclamation recovers, and a budget that is
sometimes too small strands a space.

No other executable format carries anything like this, because no other system
makes the caller pay for the kernel structures.

### 3. Nothing executable is anonymous

An `anonymous` region is memory that starts as zeros and is named by nothing. A
`named` region's content is fixed by a digest.

**An executable region may never be anonymous.** "Execute whatever happens to be
there" is not a thing a Cookie program can say — not by policy, but because the
format cannot express it. Every byte that will ever be fetched as an instruction
is named by a digest somebody signed.

This is what W^X becomes when the image describes a space rather than a file. An
ELF can map an anonymous executable page; the loader simply does it. Here the
plan has nowhere to put that request.

### 4. Fault disclosure is declared by the image

`docs/M7_11_FAULT_PRIVACY.md` reports faults by *region* and never by address,
and lets a region be `sealed` so no report is produced at all. Today whoever
sets the region up chooses.

In a `.ckx` the region chooses, in bytes the package digest covers — so a
compromised loader cannot downgrade a key-material region to `paged` to make its
faults observable to a pager. The program's author decided, and signed it.

No other executable format can express this because no other system reports
faults by region in the first place.

### 5. The authority ceiling is signed content

The header declares the **maximum authority classes this image may ever hold**.
The loader may grant less; it cannot grant more.

An `.apk` manifest fixes what an app *asks for* — what it ends up holding is a
runtime question answered by whoever grants. The interesting attacker in a
capability system is not the app, it is whatever hands the app its capabilities.
Here the ceiling is not something the granting path holds, so **a compromised
process manager cannot widen a program beyond what its author signed**. It is
also answerable without running anything: read the header, know the most it can
ever do.

## `.cookie`, and why a `.ckx` cannot vouch for itself

A `.cookie` is the application: what a user installs, what
`docs/M1_0_PACKAGE_FOUNDATION.md`'s `ApplicationIdentity` names, what the
registry records generations of. It contains one or more `.ckx` plus the
manifest and the content the digests name.

**A `.ckx` on its own is untrusted bytes.** It carries no signature. Trust comes
from the `.cookie` containing it, through the `ContentDigest` and
`SignerLineageId` the package model already binds. An image that could vouch for
itself would be a second answer to "what is this program", beside the one the
whole product already depends on — the same duplication
`docs/M7_11_MEMORY.md` refused when it declined seL4's separate derivation tree.

### Where content is verified, and why not in the kernel

Apple's platform security guide is explicit that code signing protects *"when
Mach objects are memory mapped as executable"* rather than once at install —
bytes on storage can change after an install-time check. The usual way to get
that is per-page hashes checked by the kernel as pages fault in, because in
those systems **the kernel is the pager and there is nowhere else to put them**.

Cookie's pager is a userland process. So the same property lands somewhere no
other system can put it: the pager checks the content it is about to supply
against the digest the region names, and refuses to answer if they disagree. The
kernel never learns that hashes exist, and `docs/M7_10_LINE_COUNT.md` does not
move.

That is the shape of every decision here — the same guarantee, placed outside
the trusted computing base because Cookie's structure allows it.

## The format

Fixed-size header, fixed-size region table. Nothing else. No relocations, no
symbol table, no string table, no sections, no dynamic linking, no
self-description beyond the table. A parser is a bounds check and a loop.

### Header (32 bytes)

| Field | Size | What it is |
| --- | --- | --- |
| magic | 4 | `CKX\0` |
| format version | 2 | Refused if unknown. Never "assume the newest I know". |
| region count | 2 | Bounded by a compile-time maximum. |
| entry | 8 | Where execution begins. |
| authority ceiling | 8 | The most this image may ever hold. |
| reserved | 8 | Zero. A non-zero reserved field is a refusal, not a field to ignore. |

### Each region (56 bytes)

| Field | Size | What it is |
| --- | --- | --- |
| virtual address | 8 | Page-aligned. |
| length | 8 | Whole pages, non-zero. |
| permissions | 1 | Exactly `MachinePermissions`. |
| disclosure | 1 | Exactly `FaultDisclosure`. |
| content | 1 | `anonymous` or `named`. |
| reserved | 5 | Zero. |
| digest | 32 | Zero when anonymous; the content's digest when named. |

## Rules the parser follows, and why each one

- **Refuse an unknown version.** A format that degrades gracefully is one an
  attacker picks the version of.
- **Refuse a non-zero reserved field.** A reserved field that is ignored is a
  covert channel through signed content.
- **Regions are page-aligned, non-empty, and do not wrap.**
- **Regions may not overlap** — overlap is the classic way one signed image
  contains two readings of itself.
- **Content kind and digest must agree.** An anonymous region carrying a digest
  claims content it is defined not to have; a named region with a zero digest
  names nothing while claiming to name something. Each contradicts the other.
- **No anonymous executable region**, per above.
- **The entry is four-byte aligned** — an unaligned entry names a point inside
  an instruction, the same check `docs/M7_12_ENTRY_BINDING.md` makes at the
  sealer — **and lands in a region this image itself makes executable.**
- **Nothing is sized by the input.**

The parser never dereferences the image's virtual addresses. Constructing the
space is the loader's job; mapping it is the kernel's.

## The writer has no opinion of its own

`build_ckx` encodes a plan, then **parses what it encoded and fails with
whatever the parser says**. It carries no validity rules of its own.

That is deliberate and it is the answer to the failure every format with both
halves eventually has: the day the reader and the writer disagree. A producer
that judged validity for itself can emit an image its own parser rejects — or
worse, one that *it* accepts and a second build does not, which would give one
program two digests and therefore two identities.

Because the check is a re-parse, every rule added to the parser binds the writer
on the same commit, with nobody having to remember to mirror it. The round trip
is asserted in the unit test and fuzzed differentially: for every image the
parser accepts, the writer must reproduce it field for field, and re-encoding
must be byte-stable.

## What the kernel does with a `.ckx`

Nothing. It never sees one. The parser is in `core/osimage`, outside
`core/oskernel`, and `docs/M7_10_LINE_COUNT.md` is where that is a number rather
than an assurance.

## What this does not decide

- **The `.cookie` container layout**, including how the named content sits
  beside the images and at what granularity it is hashed. That is packaging and
  belongs with M1. What is decided is the relationship: identity and signature
  live at the `.cookie` level, and a `.ckx` never certifies itself.
- **How the build produces a `.ckx`.** It has to emit regions and digests rather
  than a file layout, which is a real difference from an objcopy step and is the
  toolchain question `docs/M7_12_FIRST_PROGRAM.md` already deferred.
- **What the authority classes are.** The ceiling is a bitmask over the kernel's
  existing coarse `CallAuthority` classes. A finer vocabulary belongs to the
  capability model, not to this format — a second permission system inside the
  first is what `docs/abi.hpp` already refuses.
- **Compression, or more than one architecture per image.** Neither is needed by
  anything, and both are ways for a parser to grow.
