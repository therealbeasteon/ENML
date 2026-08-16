# M7.12 — The first program

**Status: design.** No code. `docs/M7_2_NO_DESTINATION.md` found that nothing can
move off Linux because Cookie has no userland — no syscall stubs, no image
format, no loader, and only hand-assembled instruction words have ever run at
EL0. This is the milestone that does not exist yet, named and decided before it
is written, for the same reason M7.11 decided the allocator question first: the
choices here cannot be revisited once anything depends on them.

## What has to exist

Four things, none large individually:

1. A **syscall stub library** exposing the `KernelCall` ABI to EL0.
2. A **program image format** the build produces and something loads.
3. A **loader** that places an image into an address space.
4. A **first process** that is a compiled program rather than written words.

The decisions below are about 2 and 3. 1 is mechanical, and 4 is the proof.

## Decision: the kernel does not parse program images

**Nothing in the trusted image interprets attacker-supplied program bytes.**

Two independent reasons, either sufficient.

**It is the wrong code at the wrong privilege.** A program image is a file, and
files come from outside. A parser at EL1 handling untrusted structured input is
precisely the category `docs/M4_5_FUZZING_DEPTH.md` exists for. Cookie already
learned this concretely: the M7.5d defects were a device-tree reader — firmware
supplied structured input parsed at EL1 with translation off — and five distinct
bugs hid behind each other in it.

**The no-kernel-heap decision forbids it anyway.** `docs/M7_11_MEMORY.md` settled
that the kernel has no dynamic allocator. A general image parser wants to
allocate per-segment structures sized by the input. It cannot.

This is not a novel position and should not be presented as one. **seL4's kernel
does not parse ELF either** — its elfloader does that work outside the kernel,
with ELF parsing resolved at build time and loading from an embedded archive.
The most heavily verified microkernel in existence declined to put that parser
in its trusted computing base. Cookie has strictly less reason to.

## Decision: the image format is Cookie's, not ELF

The obvious move is ELF, because every toolchain emits it. Cookie should not,
and the reason is not aesthetic.

**Cookie already has a program identity model, and it is stronger than what an
ELF loader would give.** M1.0 established `ApplicationIdentity` = `PackageId` +
trusted `SignerLineageId`, immutable `PackageGenerationRecord`s each binding a
verified `ContentDigest`, and M1.1 added a bounded manifest with an explicitly
*untrusted* analyzer boundary. M5.0 built the verified-boot chain above it.

Adopting ELF would add a **second** description of what a program is, beside the
one the whole product already depends on — a second source of truth, which is
the exact class of defect `docs/M7_11_MEMORY.md` refused when it declined seL4's
separate capability-derivation tree.

It would also mean writing an ELF parser: relocations, program headers, dynamic
sections — a format designed for a linker's convenience rather than for being
checked. Cookie needs none of that expressiveness.

**A Cookie program image is a fixed-layout table of segments** — offset, length,
virtual address, permissions — produced by the build, with the whole image
covered by the `ContentDigest` the package model already carries. The loader
copies and maps. It does not interpret. There is nothing to relocate because
images are position-fixed, and nothing to resolve because there is no dynamic
linking.

### No dynamic linking, ever

Stated now because it is much harder to remove later. A program is one image.

Dynamic linking is a code-injection surface — the loader resolves names at run
time from files the program never committed to. It defeats the content digest,
because what ran is then not what was signed. And it hands the kernel a
per-process dependency graph to reason about.

The cost is real and is accepted: every program carries its own copy of shared
code, and Cookie pays in image size and page cache. That is the right trade for
a system whose identity model is "this exact content, signed by this lineage, at
this generation".

## Decision: boot places exactly one image

The bootstrap problem is that a loader is itself a program. Something has to
place the first one.

**Boot places one image, and that image is part of the boot artifact** — covered
by the same measurement `docs/M5_0_VERIFIED_BOOT.md` describes, so what the first
program is stays answerable from the boot chain rather than from a filesystem
that does not exist yet.

This is where Cookie improves on the reference rather than copying it. seL4's
elfloader takes its user image from an unsigned CPIO archive, trusted by
construction because it is a build artifact. Cookie's boot already carries
digests and signer lineage, so its first program can be **identity-bound rather
than merely built-in**.

That matters more here than anywhere else. **The first process is the most
privileged userland thing that will ever exist on the machine** — it holds the
memory authority M7.11 grants it, and everything else is derived from it. An
unverified root task would make the entire capability model rest on an unmeasured
blob.

Every subsequent program is loaded by userland, using the package machinery M1
already built. The kernel is not involved.

## What this does not decide

- **Where a thread is allowed to begin.** Decided separately in
  `docs/M7_12_ENTRY_BINDING.md`, because it is the question this document's
  loader/image split immediately raises and it had to be settled before
  `thread_create` had a caller. The short form: the entry address is a property
  of the address space, bound when its root is sealed, and is never an argument
  to the call that creates a thread — otherwise a loader could enter signed code
  past its own initialisation while the digest above still verified.
- **The toolchain path.** How the build produces a fixed-layout image from
  compiled objects — a linker script and an objcopy step, most likely — is an
  implementation question, not an architectural one.
- **Where the first program's memory authority comes from.** That is M7.11's
  physical-authority work; this milestone consumes it rather than defining it.
  **M7.12 cannot start before address spaces can be created after boot.**
- **Whether the first process is the pager, the memory manager, or a launcher of
  those.** A structural question worth its own review; it changes nothing above.

## Exit criteria

- A compiled C++ program, built by this repository's own toolchain, runs at EL0
  on Cookie and makes a system call. Not written instruction words.
- Its image is covered by a content digest checked before it runs.
- It is loaded into an address space created after boot, not one planned by
  `plan_early_boot_memory`.
- The kernel contains no code that parses the image format — visible in the
  M7.10 line count as where the loader is *not*.
- Proven under `kernel-arm64-native`, not on the host.

## Why this is the milestone that unblocks the product

`docs/M7_2_DELINUX.md` cannot start without it. `docs/M7_2_SERVER_LOOP.md`'s
one-endpoint server model has nothing to run in. M7.11's memory authority has no
second holder to grant to. It is also the first point at which "Cookie runs
software" becomes true — a sentence every claim in `docs/PROJECT_VISION.md`
quietly assumes.
