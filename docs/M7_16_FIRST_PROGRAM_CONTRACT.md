# M7.16 — What a program is handed when it starts

**Status: decisions, before the code.** This is the same order M7.11 used for the
allocator question and M7.12 for entry binding, and for the same reason: the
contract between a loader and a program is inherited by every program that ever
runs, and it cannot be changed once two of them exist.

`docs/M7_12_ENTRY_BINDING.md` settled *where* a thread begins and explicitly
deferred *what it finds there* to the loader. This document answers that, and
one question it raises about how the proof can observe anything at all.

## The state of the world this has to fit

Three facts, measured rather than assumed:

- **Boot places exactly one image** (`docs/M7_12_FIRST_PROGRAM.md`). So the
  first program is not loaded by a loader; it *is* the loader, or the thing that
  starts one. M7.16 therefore splits cleanly: the first compiled program, and
  then a loader that places the second.
- **A `.ckx` is a plan for an address space, not a file layout**
  (`docs/M7_12_CKX_FORMAT.md`). It already declares every region, its
  permissions, its content digest and its fault disclosure.
- **A malformed call is refused rather than fatal** (M7.15c). Before that, a
  program that got its startup contract wrong halted the machine on its first
  syscall. It now gets a refusal, which is the difference between a debuggable
  first program and an unbootable one.

## Decision: a program is handed capabilities, not text

`x0` holds **one capability** and nothing else is defined. No argument vector,
no environment, no auxiliary vector.

Each of those three is declined for its own reason, and they are not the same
reason:

**No argument vector.** `argv` is a pointer to a pointer array of
NUL-terminated strings, parsed by the program at the least-tested moment of its
life — before any of its own initialisation has run, with a layout it must trust
whoever built it to have got right. Cookie has spent two milestones removing
parsers from privileged positions (the kernel does not parse images; the pager
verifies content outside the TCB). Putting one at the entry point of every
program would be the same mistake at a smaller scale.

**No environment.** An environment is *ambient authority made of strings*: it is
inherited by default, invisible in the plan, and its influence on behaviour is
undiscoverable from the image. Every other authority in this system is a
capability that something explicitly granted. An environment variable is the
opposite of that in all four respects.

**No auxiliary vector.** Linux's `auxv` exists to tell a program things about
its own address space — page size, program headers, entry point. A `.ckx`
already declares all of that, and the program was built against it. A second
description of the address space beside the plan that built it is the
second-source-of-truth defect this project has now declined three times by name.

**What the one capability is:** the program's *initial endpoint* — the single
IPC endpoint through which it asks for everything else. This follows
`docs/M7_2_SERVER_LOOP.md`'s one-endpoint model rather than inventing a startup
mechanism beside it. A program that needs memory, a device, or another service
asks; nothing is ambient.

`x1`–`x7` are zero at entry, and that is stated rather than left unspecified.
"Undefined" in a startup contract means "whatever the last thing to touch that
register left there", which is both a disclosure and a dependency waiting to be
accidentally relied on.

## Decision: the stack pointer is the caller's, and the program does not learn its own layout

`thread_create` already takes the stack and not the entry
(`docs/M7_12_ENTRY_BINDING.md`: *a caller may not choose where code begins, but
may choose where data lives*). So `sp` is set and the program uses it.

What the program is **not** told is where anything else is. It does not receive
its own base address, its region list, or its heap bounds. It was compiled
against the plan; the plan is what the loader executed; anything the program
needs to know about its own layout it knows at build time. Handing it a runtime
description would create the possibility of the two disagreeing, and the program
would have no way to tell which was right.

## Decision: the link address and the plan are derived from one source

The program is linked at the address its `.ckx` region declares, and **the build
derives one from the other rather than stating it twice.**

This is not a style preference; it is the defect PR #145 already found once. The
ARM64 Image header declared `text_offset = 0x80000` while the image was linked
at `0x40200000`, and it survived because the boot proof loads by program headers
and never reads that field. Two statements of one address, with only one of them
load-bearing, is a lie waiting for the day the other one gets read.

Same rule as `.ckx`'s construction cost, which is computed from the plan rather
than declared in it: **derived-not-stored is the security content**, because a
declared value is one an image can be wrong about.

## The problem this raises: a first program cannot say anything

Worth writing down because it changes how the milestone is proven.

Every marker Cookie's boot proof emits comes from the kernel, over a UART the
kernel discovered. **The first program has no UART capability and should never
have one** — a program that can drive a device the policy did not grant it is
the whole failure the device model exists to prevent, and granting the first
program a serial port "just for bring-up" is exactly the sort of thing that is
never removed.

So the proof that a compiled program ran cannot be a print. **It has to be a
syscall effect the kernel observes and reports.** That is a stronger proof
anyway: a print proves the program reached a library, while a syscall arriving
with the right call number, from the right thread, in the right address space,
with arguments only that program could have encoded, proves it executed.

The concrete shape: the first program issues a call whose arguments it computes,
the kernel checks the arguments are the computed ones, and the kernel emits the
marker. A hand-assembled program could pass a constant; the check should be
something a compiler had to produce.

## The x19 marker cannot be the proof, and that is a blocker

Found while starting the code, and recorded here because it invalidates the
obvious plan.

All three existing EL0 "programs" prove they ran the same way: the instruction at
their entry sets **x19** to a per-program constant, and the kernel reads
`frame->x[19]` at the `svc`. That works precisely because they are hand-written
instruction words, where `movz x19, #marker` is guaranteed to be the instruction
immediately before the trap.

**A compiled program going through `core/osabi` cannot use it.** x19 is
callee-saved under AAPCS64. The program would set x19 and then *call*
`os::abi::trap`, and if the compiler translating `syscall_aarch64.cpp` uses x19
as one of its scratch callee-saved registers, it saves the caller's value on
entry and restores it on exit — so x19 holds *osabi's* value at the moment the
`svc` executes, which is the moment the kernel samples the frame. The value is
restored by the time `trap` returns, which is too late: the observation already
happened. Nothing in the source says this will occur and nothing says it will
not, which is the worst version of it.

A file-scope `register std::uint64_t m asm("x19")` does not fix it either. That
reserves x19 within the translation unit that declares it, and `trap` is in
another one.

**Two things follow, and the second is the reusable lesson.**

The first: the first compiled program needs a proof carried in the call's *own*
argument registers, which the stub writes and the ABI defines — not in a register
set outside the call by a neighbouring instruction. `yield` takes zero arguments
and so cannot carry one, so the proof call has to be one the kernel already
decodes arguments for.

The second: this document reasoned carefully about what a program is *handed* and
not at all about how its execution is *observed*, and the observation is the part
that broke. Its own exit criteria already said the proof must be "a kernel-observed
syscall effect, not a print, and something a constant could not have produced" —
and x19 is neither: it is set outside the call, and it is a constant. **The
criteria were right and the plan quietly failed to satisfy them**, which is what a
criterion is for.

## What is deliberately still open

- **The loader itself.** Executing a `.ckx` plan against the syscall surface is
  the second half of M7.16 and is not designed here, because the first program
  does not need it — boot places one image, and the loader is what that image
  becomes.
- **Where the initial endpoint's other end is.** Something has to be listening
  before the first program sends. That is a question about what boot constructs,
  not about the startup contract, and it belongs with the loader.
- **Program exit.** `thread_exit` is in the ABI with one argument and no
  decoder. The first program does not exit — it is the root of the system — so
  deciding what an exit code means can wait for the second program, which is the
  first one for which the answer matters.

## Exit criteria

- A program compiled by this repository's toolchain, from C++ rather than from
  `uint32_t` instruction words, runs at EL0 and makes a system call.
- It receives exactly one capability, and its remaining argument registers are
  zero, checked by the kernel at first entry rather than assumed.
- Its link address and its plan's region address come from one definition, and
  a build in which they disagree fails.
- The proof is a kernel-observed syscall effect, not a print, and it is
  something a constant could not have produced.
- Proven under `kernel-arm64-native`. The M7.10 count moves only by what the
  kernel needs to check the above — the program itself is not kernel code and
  must not appear in that count.

## Where the milestone landed

Two increments, and the split is worth recording because the first one looked
finished and was not.

**M7.16a — a program a compiler produced (PRs #161, #163).**
`system/programs/first/` is a C++ translation unit built by this repository's
toolchain, reaching the kernel through `core/osabi`, gated on five properties a
program that merely linked would not have: an `svc` is present, the decoy is at
the region base and the entry at the declared offset, the entry is
instruction-aligned, the flat image fits one page, and there are zero
relocations. **Nothing ran it.** The gap is easy to understate and was stated
plainly at the time: a program that exists in the build tree and a program that
executes are separated by everything below.

**M7.16b — it runs.** The boot proof's third process no longer has eight
hand-assembled words installed into its code page; it has this program's flat
image, embedded in the boot artefact by
`core/oskernel/boot/first_program_image.S` and copied into the page its address
space maps. `COOKIE:M7.16:PROGRAM_RAN` is gated by `kernel-arm64-native`.

Against the criteria above, measured rather than claimed: all five are met.
The one worth expanding is the fourth, because it is the one the original plan
failed. The program folds a seed through eight multiply-xor rounds, reading it
through a `volatile` lvalue so the compiler must emit the arithmetic instead of
one `movz`, and carries the result in the single argument register of a call the
kernel already decodes. The kernel folds the same seed from the same header —
`cookie/first_witness.hpp`, included by both sides so there is one definition of
the algorithm and no second opinion to drift from — and compares. A
hand-assembled program could have produced the value only by containing it, and
the fold is chosen so that no single-instruction materialisation of the result
exists.

**What replacing the third process cost, stated because it is a real loss.**
That process used to carry a universe marker in `x19`, checked on every timer
interrupt alongside process A's and B's. A compiled program cannot promise a
value in a callee-saved register at the instant of an `svc` it reached through a
function call, which is the blocker recorded above, so the marker half of that
check is now excused for this thread. The thread half is not: "the scheduler
resumed something this proof never admitted" stays fatal, because it stays
exactly as serious. And what the marker stood for — that the thread entered
where its space's sealed root declared, and not at the base of its mapping — is
now established by the witness instead, which is a stronger statement made at a
better place: the decoy at the region base folds a *different* seed, so entering
at the wrong address is reported as its own failure rather than as a wrong
value.

**The address is one definition, and it is now genuinely one.** The top-level
`CMakeLists.txt` declares the base and the entry offset; the linker script is
handed both and carries no fallback of its own, the kernel image is compiled
against the same two, and the CI check reads them back out of that same file
rather than restating them. That last part was a defect this milestone
introduced and removed in the same breath: the check shipped in M7.16a with the
address written as a literal, which made it a fourth statement of a number the
whole design says must have one — and it would have kept passing against its own
copy on the day the build moved the program, which is the one day it exists for.

## What is still open after this

The list from "What is deliberately still open" above is unchanged except that
the loader is now the whole of the remaining milestone. The initial endpoint is
minted and handed over in `x0` as the contract requires, and **nothing is
listening on the other end** — where that end lives is a question about what
boot constructs, and it is the loader's to answer. Program exit is still
undecided, still for the same reason: the first program does not exit.
