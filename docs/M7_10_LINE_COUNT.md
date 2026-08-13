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
| core | 1,280 | 1,280 | The privileged portable runtime - address spaces and threads, the rendezvous, interrupt dispatch, capability transfer, the ABI |
| machine | 1,040 | 1,040 | The AArch64 port and the `machine.hpp` contract it satisfies |
| discovery | 723 | 723 | Boot-time hardware discovery: FDT parsing, hardware inventory, boot memory planning |
| entry | 352 | 352 | Reset vector, freestanding memory primitives, the boot routine |
| **total** | **3,395** | **3,395** | |

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

- `core` is **2.1x** the QNX microkernel. It must shed **675 lines** to reach 605.
- The whole trusted image is **3,395 lines**, against 15,930 for the entire QNX
  operating system including filesystem, device manager, networking and drivers.

The second comparison is the uncomfortable one and it is the honest one. Cookie
currently spends a fifth of an entire operating system's budget on a kernel that
does four things, cannot mount anything, and has no drivers. Some of that is
real: Cookie's kernel carries capability transfer, which QNX's does not, and it
is written in C++ with explicit bounds and typed results rather than in terse C.
Some of it is not real and is simply not yet compressed.

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
