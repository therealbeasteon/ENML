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
