# M7.10 - The line-count gate

`docs/M7_0_KERNEL.md` states the condition the whole kernel decision rests on:

> The measure of whether it was worth it is a single number: how much code has
> to be trusted. If ENML's kernel reaches Linux's size, the exercise has failed
> regardless of what else it achieved.

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
| core | 4,120 | 4,120 | The privileged portable runtime - address spaces and threads, the rendezvous, capability-checked interrupt attach/detach/complete over dispatch, begin_service delivery to the woken driver, capability transfer bound to execution authority (thread + address-space epoch, not thread alone), context-bound IPC endpoint authorization and generation-checked reply collection, deadline scheduling authority, generation-bound address-space epochs and process translation, native IPC endpoints/continuations/syscalls, the generation-bound address-space capability encoding and its create/destroy decoders, the capability-checked address-space create and two-phase destroy, authority over physical memory and the capability encoding that names it, the fault-disclosure region table, the ABI |
| machine | 3,232 | 3,232 | The AArch64 port and the `machine.hpp` contract it satisfies, including GICv3 device-PPI mask/unmask, interrupt-delivery completion on resume, synchronous-fault classification (abort class, fault status, translation level), and the physical ledger's reservation table — which ranges hold kernel state, of which kind, and who may map them, and reclamation zeroing a destroyed space’s ranges before releasing them |
| discovery | 1,272 | 1,272 | Boot-time hardware discovery: FDT parsing, hardware inventory, GICv3 topology, architected timer discovery (physical and virtual PPIs), boot memory planning |
| entry | 1,474 | 1,474 | Reset vector, freestanding memory primitives, the boot routine (including the minimal pre-discovery identity map that closes "the pre-MMU window," below, and the declaration of the page-table arena, the kernel's writable image and its stack as kernel state), syscall-entry decode/dispatch of the three interrupt calls, GICv3 device-source IRQ routing, the decoded fault reporter, the M7.9 end-to-end driver proof, the M7.11 address-space create/destroy proof, its EL0 syscall dispatch, and the paired non-zero-then-zero reclamation check |
| **total** | **10,098** | **10,098** | |

`core` is the number the QNX comparison is about. The others are trusted but are
not what that figure described. Since 2026-08-15 the figure it is held against
is 4,529 semicolons - QNX's microkernel plus `Proc` - rather than 605; see "The
target was restated" below for why, and for what that deliberately did not
change.

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

- `core` is **0.4x its comparable target** — **1,865 semicolons against 4,529**
  (QNX's microkernel *plus* `Proc`), with **2,664 to spare**.
- Against the microkernel alone it is **1,865 against 605** — printed too,
  because hiding the unflattering number would be the wrong kind of honesty.
  But that half does no memory management and Cookie's does; see below.
- In this gate's own metric `core` is **4,120 lines**, and the whole trusted
  image is **10,087 lines / 4,500 semicolons**.
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

### The target was restated on 2026-08-15, and what that did and did not change

`docs/M7_0_KERNEL.md` now holds `core` against **4,529 semicolons** — QNX's
microkernel plus `Proc` — rather than 605, because Cookie's kernel does the job
both of those do. This was decided when `core` reached 3,945 lines and the
open question in `docs/M7_11_MEMORY.md` asked whether a VM subsystem would push
it past 4,000.

**`core` passed 4,000 lines in the next increment, and that was a non-event
because of the order.** The fault-disclosure wiring took it to 4,120. Had the
restatement come after, it would have been a target adjusted by the change that
needed the room; coming before, it was a superseded marker being passed. The
figure that binds is 4,529 semicolons and `core` is at 1,865, so the number
worth watching says there is room - which is a different statement from "the
ceiling was raised again" and the distinction is the whole reason the two
landed separately.

The decision was made **before** the increment that would have crossed it, not
after, and that ordering is the point: a target restated in the diff that
needed the extra room is a target that was never binding.

What it did not change:

- **No ceiling moved.** The ratchet is still the measured value, so `core`
  cannot grow by one line without someone editing a number and defending it.
- **The unflattering comparison is still printed** on every run — `core`'s
  semicolons against 605 — labelled with the reason it is not the comparison
  being defended, rather than dropped.
- **The gate still says when the target is passed.** The distance is printed
  pass or fail, and the wording changes to `core has PASSED its comparable
  target` the day it does, rather than the day someone recomputes it by hand.

What remains uncomfortable, and should: a kernel that cannot mount anything and
has no drivers is spending 1,865 semicolons where a shipped 1992 system spent
4,529 on the same responsibilities *plus* a filesystem-grade resource manager.
Being under the target is not the same as being small.

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
