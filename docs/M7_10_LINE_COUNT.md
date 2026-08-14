# M7.10 - The line-count gate

`docs/M7_0_KERNEL.md` states the condition the whole kernel decision rests on:

> The measure of whether it was worth it is a single number: how many lines of
> code have to be trusted. If ENML's kernel reaches Linux's size, the exercise
> has failed regardless of what else it achieved.

Nothing measured that number. It was a sentence in a document, which is the
weakest form a constraint can take. `.github/scripts/kernel-line-count.sh`
measures it on every push and fails the build when it grows.

## What is counted

The boundary is **code that executes with kernel privilege in the shipped
image** - concretely, the source list of the `cookie_kernel_aarch64_boot` target
in `core/oskernel/CMakeLists.txt`, plus the headers those files depend on. The
build already draws that line, so the measurement cannot drift from what is
actually trusted, and a reviewer can check the script against one CMake target
rather than against a judgement.

Four categories, counted and capped separately so that the number is legible and
so that lines cannot be moved between categories to get under a ceiling:

| Category | Lines | Ceiling | What it is |
| --- | --- | --- | --- |
| core | 3,419 | 3,419 | The privileged portable runtime - address spaces and threads, the rendezvous, capability-checked interrupt attach/detach/complete over dispatch, begin_service delivery to the woken driver, capability transfer bound to execution authority (thread + address-space epoch, not thread alone), context-bound IPC endpoint authorization and generation-checked reply collection, deadline scheduling authority, generation-bound address-space epochs and process translation, native IPC endpoints/continuations/syscalls, the ABI |
| machine | 3,010 | 3,010 | The AArch64 port and the `machine.hpp` contract it satisfies, including GICv3 device-PPI mask/unmask, interrupt-delivery completion on resume, synchronous-fault classification (abort class, fault status, translation level), and the physical ledger's reservation table — which ranges hold kernel state, of which kind, and who may map them |
| discovery | 1,272 | 1,272 | Boot-time hardware discovery: FDT parsing, hardware inventory, GICv3 topology, architected timer discovery (physical and virtual PPIs), boot memory planning |
| entry | 1,122 | 1,122 | Reset vector, freestanding memory primitives, the boot routine (including the minimal pre-discovery identity map that closes "the pre-MMU window," below, and the declaration of the page-table arena, the kernel's writable image and its stack as kernel state), syscall-entry decode/dispatch of the three interrupt calls, GICv3 device-source IRQ routing, the decoded fault reporter, the M7.9 end-to-end driver proof |
| **total** | **8,823** | **8,823** | |

`core` is the number comparable to QNX's 605. The others are trusted but are not
what that figure described.

**Discovery is counted, and the argument for excluding it is rejected here.**
The argument is decent: FDT parsing and hardware inventory run once, before any
user process exists, and could one day be a boot service outside the kernel. But
today they run at EL1 with translation off, parsing a blob supplied by firmware,
and a defect in them is a defect in the most privileged code on the machine.
Excluding them would have cut 723 lines off the headline by relabelling rather
than by deleting anything. They get their own category only so that the day they
do move out of the kernel shows up as a drop in one number.

**One thing under `core/oskernel/` is excluded**: `machine_host.*`, the host
stand-in for the machine layer used by the tests. It is not in the boot image
and never runs on a device. It is listed by name in the script rather than
silently omitted, so the exclusion is a decision on the page.

Any file under `core/oskernel/` in neither a category nor the exclusion list
fails the check. A new kernel source file cannot be added without someone
deciding, in the script, what it is.

Blank lines and comments are excluded. The reference number is a code size, so
counting comments would compare different things; and a ceiling that counts
comments taxes explanation, which in a kernel whose entire case is
reviewability is the wrong incentive.

## The ceiling is the current count, not the target

The ceilings above are the measured values at the commit that added this gate.
They are not what the project wants.

Setting them at 605 would make the gate red on the day it landed, and a gate
that is red on arrival gets switched off. Setting them comfortably above the
current count would make the gate measure nothing, which is the more common
failure and the harder one to notice. Set at the measured value it is a ratchet:
the kernel is free to shrink and free to be rewritten, and it cannot grow
without someone editing a number in the script and defending that edit in
review.

Raising a ceiling is allowed and will happen - the machine layer is not
finished, and work already in flight adds to it. The requirement is that the
raise lands in the same change as the lines, so that the growth and the decision
to permit it are one reviewable diff instead of a drift nobody voted for.

## The gap

The script prints this on every run, pass or fail, so the distance stays visible
rather than becoming something the project stopped mentioning:

- `core` is **2.6x** the QNX microkernel measured the way QNX measured itself —
  **1,583 semicolons against 605**.
- In this gate's own metric `core` is **3,419 lines**, and the whole trusted
  image is **8,823 lines / 3,924 semicolons**.
- QNX for scale, all semicolons: microkernel **605** *(no memory management)*,
  `Proc` **3,924**, whole OS **15,930**.

### The comparison was wrong twice, and both corrections landed together

`docs/REFERENCE_NOTES_2026_08_14_QNX.md` records this in full. It happened
because the QNX paper had been cited for a year and not read.

**Metric.** QNX's figures are semicolon counts — the paper says so in the
sentence introducing its own table. Dividing Cookie's non-blank non-comment line
count by 605 compared two different quantities and roughly doubled the apparent
gap. The stripped text is now counted both ways by the same pass, so the two
numbers cannot drift.

**Scope.** The QNX microkernel implements four services — IPC, low-level network
communication, process scheduling, interrupt dispatching — and **memory
management is not among them**. It lives in `Proc`, a user-space resource
manager of 3,924 semicolons. M7.11 is putting memory management *inside*
Cookie's kernel, so 605 stopped being the comparable figure the moment that work
started; 605 + `Proc` is.

It also cuts against Cookie once, and that is printed too: QNX's microkernel
carries low-level networking, which Cookie's `core` does not and will not.

**None of this is headroom, and no ceiling moved because of it.** The ratchet
measures Cookie against its own past in one metric, and that job never depended
on what QNX counted. What changed is only what this gate is entitled to claim.
The claim that remains is still uncomfortable and still honest: 2.6× a 1992
realtime microkernel, for a kernel that cannot mount anything and has no
drivers, is a number to defend rather than celebrate.

## What this does not measure

- **Lines are not review effort.** A 200-line page-table walker is harder to
  audit than 600 lines of straight-line dispatch. The count is a proxy that can
  be gamed by writing denser code, and nothing here detects that.
- **Nothing outside `core/oskernel/`.** The kernel links `emnl::oscore`, and
  those lines are trusted too. They are not counted here because the boundary
  chosen was the boot image's own sources; extending the count to shared
  libraries is a later decision, and until it is made the total understates the
  TCB.
- **The comment stripper is lexical.** A `//` inside a string literal would be
  mistaken for a comment. There is no such literal in the kernel today, and a
  stripper that parsed C++ properly would be a larger thing to trust than the
  code it measures.
