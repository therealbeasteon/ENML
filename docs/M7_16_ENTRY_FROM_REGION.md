# M7.16 — execution authority belongs to a region, not to an address

**Status: decided here. This replaces the three options the audit
(`docs/AUDIT_2026_08_17_LOADER_PATH.md`) put forward, and none of them is the
answer.**

## The problem, restated in one paragraph

A loader must be able to make a program it built runnable. `thread_create`
deliberately takes no entry point — `docs/M7_12_ENTRY_BINDING.md` — because a
principal holding a space full of code it did not write must not choose where
that code begins. The entry is instead bound when the space's translation root
is *sealed*, and **nothing in the sixteen-call ABI seals a root**. So a space
created from EL0 can never acquire an entry, and no thread can ever be admitted
into it.

The three obvious closures were: add a seventeenth `seal` call; fold the entry
into `address_space_create`; or move the whole operation into a process-manager
service. Each of them answers the question *"who is allowed to name the entry?"*
That is the wrong question, and the references say so.

## What the references actually do

**QNX** (Hildebrand 1992, `qnx-paper92.pdf`) is the shape `docs/M7_0_KERNEL.md`
holds up: four services, **14 kernel calls**, and **process creation is not one
of them** — it lives in `Proc`, a user-space resource manager, described as "the
first, and only mandatory" one. That is a real argument for the service option,
and it is also incomplete for this problem: `Proc` still needs some kernel-level
means to make a newly-populated address space runnable. QNX does not have to
answer how, because it predates the threat model in which the loader is not
trusted with the code it loads.

**seL4** hands the choice to the creator outright — `seL4_TCB_WriteRegisters`
sets PC, SP and PSTATE freely. Cookie already recorded why it declines to copy
that: seL4's loader is part of the trusted image, and Cookie's is not.

**Apple's Secure Enclave Boot Monitor** (`apple-platform-security-guide.pdf`,
p.13) is the one that answers the question, and it answers it by not asking it:

> To make the loaded sepOS executable, the Secure Enclave Boot ROM sends the
> Boot Monitor a request with **the address and size** of the loaded sepOS. On
> receipt of the request, the Boot Monitor resets the Secure Enclave Processor,
> **hashes** the loaded sepOS, updates the SCIP settings to allow execution of
> the loaded sepOS, and **starts execution within the newly loaded code**. […]
> this same process is used whenever new code is made executable.

The requester supplies a **region** — address and size — and never an entry.
Execution begins *within the region* as a consequence of the region becoming
executable. There is no entry point to authorise because there is no entry point
in the interface.

Two more pieces of the same system point the same way. **SCIP** configures each
coprocessor's memory unit to prevent "executable mappings outside its part of
the protected memory region" and "writeable mappings inside" it — W^X expressed
as a property of a *region*. And **PPL/SPTM** exist to "prevent user space code
from being modified after code signature verification is complete", managing
page-table permission overrides from a privilege level above the kernel. The
thing all three protect is **which memory may be executable**. None of them
protects an address.

## The decision

**Cookie does not have an entry argument anywhere, and does not gain a `seal`
call. The entry is derived by the kernel from the address space's own executable
region.**

- A space records its executable region when one is established — which is
  already an operation Cookie has, and the only one that can create executable
  memory: `map` with `MapPermissions::read_execute`. That call is Cookie's
  equivalent of the Boot Monitor request, and it already carries exactly what
  the Boot Monitor is given: an address, a length, and the intent to make memory
  executable.
- **A space may have at most one executable region.** A second `read_execute`
  mapping into a space that already has one is refused.
- When a thread is admitted, the entry is **the base of that region**. Nothing
  supplies it, so nothing can supply a wrong one.
- A space with no executable region cannot have a thread admitted into it, and
  the refusal names that rather than pretending the space is malformed.

The ABI stays at **16 calls**, which is what `abi.hpp`'s standing instruction —
"Reaching it is a signal to remove something or to move it into a service, never
to raise the number" — actually asked for. Nothing was moved into a service and
nothing was removed; the operation turned out not to be needed.

## Why this is better than the three it replaces

The other three all *authorise* an entry: they decide which principal may name
one and refuse the others. This one makes an entry **unrepresentable**. M7.12's
attack — a loader entering signed code past its own initialisation while the
content digest still verifies — is not refused, it cannot be expressed. There is
no argument to put the wrong address in.

That is the same move Cookie has already made four times, and it is worth naming
as the pattern rather than as a coincidence: **no `argv`** (deletes a string
parser at a program's least-tested moment), **the program does not learn its own
layout** (deletes a disagreement between two descriptions), **`map`'s length
comes from the grant** (deletes a reconciling check), **the outcome tag is its
own register** (deletes a reserved range in the value space). Each removed a
capability rather than guarding it. This removes the fifth.

It is also cheaper than all three: no new call, no changed argument count, no
new service, and one fewer thing for a reviewer of the kernel surface to hold in
their head.

## Where Cookie improves on the reference rather than copying it

Apple's Boot Monitor **hashes the region at the moment it becomes executable**,
because at that moment the region's content is the only evidence of what is
about to run. Cookie does not need to: **a `.ckx` region is content-addressed by
digest already** (`docs/M7_12_CKX_FORMAT.md` — "content is addressed by digest,
so the bytes may come from anywhere"), and M1.0 binds that digest to a signer
lineage. The measurement is in the plan rather than recomputed at the transition,
which is strictly stronger: Apple's monitor learns *what* is about to execute,
and Cookie's loader was handed memory that could only have come from content
matching a signed digest.

That difference also fixes the one thing the Boot Monitor pattern leaves open.
"Starts execution within the newly loaded code" is vague about *where* within;
for a boot ROM handing off to one image, it does not matter. For a general
loader it would, and Cookie's answer is exact and checkable: the base of the
region, which is the first byte of the content the digest covers.

## What this constrains, stated plainly

**One executable region per address space.** That is a real constraint and it is
one Cookie had already taken on without writing it down: `docs/M7_12_FIRST_PROGRAM.md`
refuses dynamic linking outright — "no dynamic linking, ever… every program
carries its own copy of shared code" — and a program with one copy of all its
code has one text region. A system that later wants shared libraries cannot have
them by relaxing this; it would have to revisit the linking decision first,
which is the correct order.

**Execution begins at the first instruction of the text region.** A program
whose entry is not its first instruction cannot be expressed. Flat images
already work this way, and `.ckx` is a flat plan.

## Consequences to handle, and not in this document's diff

- **The first program's decoy becomes unnecessary.** `system/programs/first`
  places a decoy at the region base and its real entry at +0x200, precisely so a
  kernel entering at the base rather than at the sealed entry is caught. Under
  this decision those are the same address and the check is vacuous — because
  the bug it guards against is no longer constructible. The decoy and the
  `COOKIE_FIRST_ENTRY_OFFSET` machinery should be removed, and the witness
  (which proves the compiled program *ran*) kept. That is its own diff, because
  deleting a proof is exactly the change that should not ride along with
  anything.
- **`TranslationRootSealer::seal` keeps its entry parameter**, and its caller
  changes: the entry passed is the one the kernel derived, not one a caller
  chose. The sealer's own refusals — zero, unaligned, non-user — stay, and stop
  being the only thing standing between a caller and a bad entry.
- **`unmap` interacts with this** and must not be written without deciding it:
  unmapping a space's executable region either clears the recorded entry or is
  refused. Refusing is the safer default and should be the starting position.

## Exit criteria

- No kernel call anywhere in the surface takes an entry point, and the surface
  is still 16 calls.
- A second `read_execute` mapping into a space that already has an executable
  region is refused, with a distinct code, proven by a test that fails when the
  check is removed.
- A thread admitted into a space enters at the base of that space's executable
  region, and a space with no executable region refuses admission.
- Proven under `kernel-arm64-native`, with the M7.10 count moved in the diff
  that causes it.
