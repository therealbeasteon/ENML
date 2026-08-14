# Cookie OS — roadmap to completion

The operating system is **Cookie**; its microkernel is the **Cookie Kernel**;
the security architecture both carry is **EMNL**. See `docs/NAMING.md`.

This document is the plan of record from 2026-08-13 through a shippable device.
It exists because the project had reached a state that individual milestone
documents could not describe: a mature security architecture, an emerging
kernel, and twenty-seven unmerged pull requests holding most of a month's work
outside `main`. A roadmap that does not start by naming that is decoration.

Milestone docs remain authoritative for their own contents. This document owns
only the *order*, the *exit criteria*, and the *honest position* at each phase.

## Where the project actually is

Measured on `main` at `ed26bf6`, not claimed:

| Layer | State |
| --- | --- |
| Build, IPC, OSIDL, supervisor, sandbox (M0) | Complete, frozen gate |
| Package and application lifecycle (M1) | Complete |
| Storage, keys, broker, runtime session (M2) | Complete |
| Display, compositor, semantic UI (M3) | Complete |
| Trusted phone shell and product security (M4) | Through M4.15 merged; **exit criteria not all verified** |
| Verified boot evidence (M5) | Designed and tested; nothing produces the evidence |
| Device access, time protection (M6) | Policy complete; **no platform enforces it** |
| Cookie Kernel (M7) | Through M7.9 merged; **boots on QEMU virt and schedules two isolated EL0 processes across native IPC and timer preemption, and a real driver process attaches to, is woken by, services and completes a device interrupt** |

Roughly 49,000 lines of implementation against 29,000 lines of test, both grown
by the backlog rather than by new work. Zero `TODO`/`FIXME`/`HACK` markers in
the tree. Thirteen CI workflows — twelve on every push and pull request, plus
nightly fuzzing on a schedule — all green on `main`. The discipline is real and
the roadmap should not spend it.

Three statements that must stay attached to any claim about Cookie:

1. **Nothing has ever run on physical hardware.** Every property in the tree is
   established on host or emulated builds.
2. **The kernel is not yet the substrate.** Linux is still underneath the
   service layer. M7 has built the kernel beside the system, not under it.
3. **The security architecture is further along than the system it secures.**
   EMNL's identities, capabilities, wire formats and policies are mature. The
   mechanisms that would make them enforceable on a real phone — IOMMU, TEE,
   measured boot, a radio boundary — are not built.

## Phase ordering rationale

The phases below are ordered by *what unblocks what*, not by interest. Two
constraints drive the whole sequence:

- **The backlog is the critical path.** Nothing in M8+ can be planned honestly
  while a third of the project's work sits in draft PRs whose merge order is
  itself a dependency graph. Phase 0 is not housekeeping.
- **The kernel must become the substrate before hardware work means anything.**
  Bringing up a board against Linux and then replacing Linux is doing the
  bring-up twice. Phase 2 precedes Phase 4 for that reason alone.

---

## Phase 0 — Stabilise: land the backlog *(complete)*

Twenty-seven open draft PRs in three stacked chains plus one orphan. Every
stack was blocked at its base on a small mechanical defect, not a design
problem. Diagnosed:

| Stack | PRs | Blocking defect |
| --- | --- | --- |
| M4.10i → M4.10t (encrypted profile storage) | #34–#45 | `profile_protector.hpp:77` uses `os::core::ErrorDomain::boot`, which does not exist. The four existing `osboot` sources all use `ErrorDomain::security`. |
| M4.11 → M4.15 (recovery, networking, leases) | #47–#50, #58 | `recovery_policy_test` is registered with CTest but absent from the build-target list in `.github/workflows/m4-shell.yml`, so it reports `Not Run`. |
| M7.5d → M7.8 (AArch64 kernel) | #52–#61 | `kernel-arm64-native` failing at the base of the stack. |
| M4.1 (supervised phone shell) | #28 | None — fully green since 2026-08-10, simply never merged. |

Every stack base was also behind `main` and needed rebasing.

### Stranded merges

Three milestones reported as merged on GitHub while being **absent from
`main`**:

| Milestone | PR | Merged into | Recovered by |
| --- | --- | --- | --- |
| M4.10h authenticated profile storage format | #33 | `m4-10g` | `m4-10i` (PR #34) |
| M7.5b real AArch64 exception entry | #46 | `m7-5` | `m7-5d` (PR #52) |
| M7.5c AArch64 stage-1 translation | #51 | `m7-5b` | `m7-5d` (PR #52) |

The cause was a merge-ordering race, and the M4.10h case shows it precisely: PR
#32 merged `m4-10g` into `main` at 11:58:17, and PR #33 merged `m4-10h` into
`m4-10g` at 11:58:24. Seven seconds decided whether a milestone shipped. The
parent had already left, so the child merged into a branch nothing was going to
read again.

All three are now genuinely in `main`. Nothing was lost: every stranded commit
survived in the open stack branch above it, and landing the stacks bottom-up
recovered them. The rule the incident produced outlives it, because the same
race recurred later the same day: **a closed PR is not evidence that its work is
in `main`.** Check ancestry. `git merge-base --is-ancestor origin/<branch>
origin/main` answers it in one command, and the twelve green workflows on `main`
did not, because code that never arrived cannot fail a gate.

**Order of work:** #28 landed first (green, no dependants), then each stack
bottom-up, fixing the base defect and rebasing before pushing the next link.
A stack was never merged out of order to save time; a rebase that skips a link
silently rewrites the link below it.

### What the phase cost and what it found

It cost a day, inside the "days, not weeks" estimate at the foot of this
document, because the defects were mechanical and the CI was comprehensive. It
added no capability. What it produced beyond the merges are two records worth
keeping.

The first is the stranded merges above, and the ancestry check that would have
caught them.

The second is a defect class rather than a defect: **a CI workflow that names
its build targets in a hardcoded list drifts from the test registry silently.**
`recovery_policy_test` was registered with CTest and missing from the target
list in `.github/workflows/m4-shell.yml`, so it never built and `ctest` reported
it as `Not Run` — a result that reads as a pass to anything scanning for
failures. The same list failed a second time in the other direction, when a
conflict resolution left conflict markers inside it and PR #63 had to remove
them. A list maintained by hand alongside a registry it does not derive from
will diverge from it, and each divergence is invisible until something looks for
an absence rather than for a failure.

One trap belongs beside it because it is what let the markers through: filtering
a repository scan with `grep -v "^./.git"` also discards everything under
`.github`, since `.git` is a prefix of it. Path filters must match a path
component, not a string prefix. The scan that catches this class is

```sh
git grep -n -E '^(<<<<<<<|=======|>>>>>>>)'
```

**Exit criteria, and where they landed**

- Zero open PRs whose only obstacle is a stale base or a missing build target —
  met with one exception, and the exception is the same defect class again.
  PR #53 (M7.5e) was closed unmerged when its base branch `m7-5d` was deleted on
  merge, which is what GitHub does to a pull request whose base disappears. The
  branch survives and the whole open M7.5f–M7.8 stack is rooted on it, so the
  chain's base has no pull request. That obstacle is mechanical, not design.
- `main` green across all twelve push workflows after each stack lands — met at
  the tip, with the conflict-marker interruption above in the middle of it.
- 59 already-merged remote branches deleted; only live work visible — exceeded;
  65 were deleted in one pass, and 73 across the day counting the automatic
  delete-on-merge. Twenty-six remote branches remain and not all are live: the
  sweep left behind heads whose work reached `main` long ago, among them
  `m0-10-arm64-gate`, `m2-1-storage-service-handles` and
  `m2-2-storage-integration`.
- `AGENTS.md` and `README.md` describing the tree that exists — half met.
  `README.md` is resynced. `AGENTS.md` is not: it still reads "M4.0-M4.10g" and
  "M7.0-M7.5a", still says the Cookie Kernel exists through M7.5c, and still
  carries a **Stranded work** section asserting that M4.10h, M7.5b and M7.5c are
  absent from `main`, which stopped being true when the stacks landed. It is out
  of scope for this change and it is the next thing to correct, because it is
  the file agents are instructed to read first.

**Honest position:** this phase added no capability. It converted work that had
already been done into work that can be built on, which is the only reason it
outranked everything below it.

---

## Phase 1 — Complete the M4 product security line *(code merged; exit criteria not met)*

The security features a user actually touches. Most of the code landed in Phase
0; this phase finishes what the stacks left open.

M4.1, M4.10h through M4.10t, and M4.11 through M4.15 are all in `main`. That is
the whole of the line as written, and it is worth being exact about what that
does and does not mean: every milestone this phase names is merged, and not one
of the phase's three exit criteria is met. The distinction matters more here
than anywhere else in the document, because the features involved are the ones a
user would be told to rely on.

- Encrypted profile storage end-to-end: boot-bound unlock, protector restore,
  rollback-bound snapshots, the protected Storage cutover seam (M4.10i–t).
- Coercion-resistant unlock and durable duress erasure, already merged, promoted
  from mechanism to a reviewed product feature with its own threat model.
  `PROJECT_VISION.md` forbids shipping a second-PIN scheme labelled coercion
  resistant; this is where that debt is paid or the feature is cut.
- User-restartable recovery domains (M4.11).
- Network blindness and authenticated connection admission (M4.14, M4.15) —
  the kernel-enforced boundary that makes "this application cannot see the
  network" a mechanism rather than a policy note.

**Exit criteria:** a profile's durable data is unreadable without both the boot
chain and the user's credential; duress erasure destroys the profile root
irrecoverably and is proven by test; no application reaches a socket the policy
did not grant.

**Against those criteria, measured rather than claimed: none of the three is met
yet.** The mechanisms exist and are tested. The criteria are claims about the
product, and each fails on something other than the mechanism.

*A profile's durable data is unreadable without both the boot chain and the
user's credential* — **not met**, and the reason is a wire that was never
connected. M4.10t added `ProtectedReplaceHandler` to `StorageService` as a
constructor parameter and a member, and nothing in the tree passes one or calls
it; the only three references to it are its declaration, its initialiser and its
field. PR #45 said so in its own body — the service still calls its existing
substrate `atomic_replace` — and that remains true on `main`. The encryption
engine below the seam is real and tested. Durable Storage writes do not reach
it. Separately, the only key provider in the tree is the OpenSSL test provider
under `core/oskeys/testing/`, whose fixed wrapping key its own header calls
deliberately test-only, so even once the seam is wired the boot-chain half of
this criterion terminates in a test root until Phase 5.

*Duress erasure destroys the profile root irrecoverably and is proven by test* —
**mechanism met, label not earned.** The erasure path is merged and covered.
What is unpaid is the second bullet above, and it should not be allowed to blur:
`PROJECT_VISION.md` forbids shipping a second-PIN scheme labelled coercion
resistant, and M4.10's coercion-resistant unlock is merged without the reviewed
threat model that would either justify the label or remove it. Until that debt
is paid the feature is a mechanism in the tree, not a product feature, and
nothing should describe it to a user as coercion resistance. The choice at the
end of this phase remains the one already stated: pay the debt or cut the
feature.

*No application reaches a socket the policy did not grant* — **not met, and not
currently falsifiable.** `core/osnetwork` is admission policy: `constexpr`
predicates over blindness plans, tunnel authority, packet and link grants, with
the supervisor holding connection admission and session state. There is no
network stack, no link driver and no socket path beneath any of it, so no
application reaches a socket at all. That is a stronger position than the
criterion asks for and a much weaker one than it means: nothing has been
enforced because nothing has yet been attempted. The bullet above also calls
this boundary kernel-enforced, which it is not — the Cookie Kernel is not the
substrate, and M4.14's own document places enforcement at a kernel/device
boundary that does not exist yet. It is a policy note that is *written to
become* a mechanism in Phase 3.

**Honest position:** duress erasure is only as durable as the storage medium's
erase semantics. Until Phase 4 puts this on real flash with a real controller,
the guarantee is "the key is gone from the abstraction we built", which is not
the same as "the bytes are gone from the die".

---

## Phase 2 — Cookie Kernel to self-hosting *(in progress)*

Finish the AArch64 kernel until it can run Cookie's own services. `docs/M7_0_KERNEL.md`
already fixed the order — ABI, host-testable core, machine layer, emulator boot —
and all four are now underway rather than three.

**The kernel boots.** M7.5d landed and the Cookie Kernel starts on AArch64 under
QEMU virt, unassisted: exception vectors installed, the device tree parsed,
hardware discovered, the PL011 UART found, page tables built, stage-1
translation enabled, and a context switch onto the guarded runtime stack. The
markers `COOKIE:M7.5d:MMU` and `COOKIE:M7.5d:GUARDED` are what the boot gate
watches for. This is the first point in the project at which the kernel is an
artefact that runs rather than a library that is tested.

The M7.5e–M7.8 stack is landed *(complete, as of `main` at `ed26bf6`)*.
Descriptor teardown and TLBI (M7.5e — PR #53 had closed unmerged when its base
branch was deleted on merge; PR #71 relanded the same work from the surviving
branch rather than rewriting it, the ancestry rule from Phase 0 applied to the
phase that follows it), the first real EL0 process and syscall return
(M7.5f), GICv3 timer delivery (M7.5g), a tickless preemptive scheduler
(M7.5h), generation-bound address-space epochs with ASID quarantine (M7.5i),
capability-addressed native IPC with reply seals (M7.6a), a stable
translation domain (M7.7), and execution authority bound to address-space
generations (M7.8) are all in `main`. Seven PRs, seven relands: each stack
branch was based on stale content and needed the same treatment Phase 0
established — merge `main` in, resolve conflicts by understanding intent
rather than picking a side mechanically, fix the mechanical defect blocking
it, verify, land, move to the next. Two defects were genuinely novel rather
than stale-base noise and are worth carrying forward as lessons: a second,
independent DTB walker (`gic_v3_discovery.cpp`) that hadn't inherited the
M7.5d cells-tolerance fix already present in `hardware_inventory.cpp`, and a
namespace-scope reference (`boot_scheduler`) initialized through a
non-constexpr accessor call, which requires dynamic initialization the
freestanding image's linker script forbids by design. Both are recorded in
project memory so they don't need rediscovering.

### The pre-MMU window

A standing hazard, recorded here because it is a property of the architecture
rather than a bug that was fixed once. From `_start` until
`activate_stage1_translation` returns, translation is off, and with translation
off every data access takes the **Device-nGnRnE** attribute regardless of what
the page tables will later say. Device memory does not permit unaligned access.
`SCTLR_EL1.A` is irrelevant to this: that bit governs alignment checking for
Normal memory, so clearing it buys no tolerance the memory type never had.

The consequence is that ordinary C++ is not safe in that window. The compiler is
free to emit an unaligned load or a multi-register copy for a plain struct
assignment, and one did — inside `FdtView::parse`, building its `Result` return
value — faulting before any of the code that could have reported it ran. The
image is therefore compiled `-mstrict-align`, which
`core/oskernel/CMakeLists.txt` documents as a requirement rather than a
hardening preference.

That left an open design question: **shorten the window, or keep compiling for
it.** It is decided now, closing M7.10's own line-count raise for the change:
shortened.

`cookie_aarch64_boot_main` builds and activates a second, minimal identity map
before `FdtView::parse` runs, then activates the real, fully discovered map
over it exactly as before — so FDT reading, hardware discovery and boot
memory planning all run on Normal memory with the alignment rules the
compiler assumes, and `-mstrict-align` stops being the thing correctness
rests on for that code. What made shortening cheap rather than merely
possible is a capability the reference implementations this hazard was
checked against — TF-A, Linux's `head.S` — do not have at their equivalent
point: they map a conservative fixed window, generous enough to cover a
device tree they have not yet measured, because they have no cheaper option
that early. Cookie already had one. `bounded_dtb`'s header read — four
individual byte loads through `read_be32`, alignment-safe by construction
where a struct-based parse is not — was already computing the DTB's *exact*
physical extent before this change, to size the `ByteSpan` handed to
`FdtView::parse`. The minimal map reuses that exact extent instead of a
ceiling: it maps precisely the kernel's own image, by segment (so
`.text`/`.rodata`/`.data`+`.bss` keep the same RX/R/RW split the real map
gives them), and precisely the DTB's measured bytes. Nothing wider, because
nothing wider was ever needed to answer this question — only unmeasured.

The two identity maps' page tables come from independent arenas — the
minimal map cannot use `plan.value().page_tables`, which is itself a product
of the boot memory plan the map exists to run before — and each gets its own
`MachinePhysicalLedger`. Sharing one was tried first and is wrong: the two
maps genuinely disagree about permissions on the same physical memory, since
each process's code page is writable in the early map (the boot routine
installs a program into it) and read-execute in the real one (the process
runs it). A shared ledger classifies that pair as a
`writable_executable_alias` and refuses it — correctly, on the information a
ledger has. The maps are sequential rather than concurrent and the early one
is abandoned before any of that memory becomes executable, but that is a
fact about boot order which no cross-space check can see. Separating the
ledgers states the boundary rather than weakening the check.

`activate_stage1_translation` runs twice as a result, once for each map —
confirmed safe to call a second time before relying on it: it unconditionally
reprograms `MAIR_EL1`/`TCR_EL1`/`TTBR0_EL1` and re-invalidates the TLB
rather than assuming anything about prior state, and both maps identity-map
the currently-executing kernel image identically, so nothing the CPU is
using moves under it.

**"Minimal" is also the hazard, and this is the part worth reading before
touching this code.** From the first activation until the real map replaces
it, the minimal map *is* the entire address space. Under the old arrangement
translation was off, so any physical address the boot routine discovered was
addressable the moment it was known; now it is addressable only if something
mapped it. Every region chosen out of discovered RAM therefore has to be
added as it becomes known — `extend_early_identity_map` does this for the
page-table region, the four process pages, and the console. The console is
the one that matters most: the first version of this change omitted it, and
the resulting Data Abort vectored to a handler whose own `uart_write` took
the same abort, so the machine looped in the vector printing nothing. A
missing mapping here does not corrupt anything, but it can destroy the
ability to report that it happened, which is the failure mode this whole
section of the boot path is built to avoid.

`-mstrict-align` stays, and `core/oskernel/CMakeLists.txt` keeps documenting
it as a requirement. Shortening the window did not eliminate it: `_start`,
`install_exception_vectors`, `bounded_dtb` and the map construction itself
all still execute before the first activation, and that code is now the
window rather than most of boot. The flag is what protects the remainder. It
is a much smaller thing to depend on than it was — a fixed prologue of
link-time-known work, not the largest and most data-driven part of boot —
which is the actual result of this change: the dependency is bounded, not
removed. Removing the flag would be a separate decision needing its own
evidence, and this change does not make it.

Then M7.9, which the roadmap called unstarted for longer than it was true and
is now done. It has a design document, `docs/M7_9_USER_SPACE_DRIVERS.md`, and
all four of its named gaps are closed; M7.10 is built and enforced by this
change:

- **M7.9 — user-space driver framework.** Interrupt handlers inside driver
  processes, connected to a vector by a kernel call, under M6.0 device access
  policy. This is the piece that makes "no drivers in the kernel" true rather
  than aspirational. Done: capability-gated `interrupt_attach`/
  `interrupt_detach`/`interrupt_complete` at the `Kernel` composition layer,
  host-tested; `cookie_kernel_syscall_entry` decoding and dispatching all
  three from EL0; machine-layer GICv3 routing of a real device source — the
  discovered virtual-timer PPI, reused as a stand-in — through
  `Kernel::dispatch_interrupt()`, mask-until-complete enforced at the
  controller by the same syscall handlers; a gap review found rather than a
  milestone named - `begin_service` was documented as riding the driver's
  wakeup but nothing ever called it, closed by `InterruptDeliveryTable` and
  `complete_interrupt_current`; and the end-to-end proof itself -
  `kernel-arm64-native` gates on `COOKIE:M7.9:ATTACHED` →
  `..:DISPATCHED` → `..:SERVICED` → `..:COMPLETED`, process A (reused after
  its M7.6a role concludes, not a third address space) attaching to a real
  capability, arming the reused virtual-timer PPI as the device it just
  attached to, and being redirected by `complete_after_switch` straight into
  `interrupt_complete` the instant its delivery arrives - the same
  `elr_el1`-rewrite technique the existing send/receive redirects already
  use, so nothing new to this proof needed an instruction pattern M7.5f-M7.6a
  had not already proven under CI.
- **M7.10 — the line count gate.** Done: `.github/scripts/kernel-line-count.sh`
  counts what runs with kernel privilege in the shipped image and fails the
  build when it grows. `docs/M7_10_LINE_COUNT.md` records the boundary. The
  ceiling is the measured 8,463 lines, a ratchet rather than the 605-line
  aspiration, and the script prints the gap to 605 on every run so it stays
  visible.

### The number M7.10 enforces

`docs/M7_0_KERNEL.md` names the QNX class of artefact as the target: an entire
operating system in 15,930 lines on a **605-line kernel**. The question that
blocked the gate was not what the ceiling should be but *what gets counted*,
because a measure that excludes whatever is currently inconvenient enforces
nothing, and one that counts every line in the directory gets argued with the
first time it fails.

The difficulty was never arithmetic. Counting C++ under `core/oskernel/src` and
`core/oskernel/include` gives 5,091; adding the two assembly files beside them
gives 5,240; adding `core/oskernel/boot`, which is the code that actually
reaches the `COOKIE:M7.5d:MMU` marker, gives 5,733. Three defensible numbers for
the same question, and picking among them after the fact is how a ceiling gets
chosen to fit the code rather than the other way round.

That decision is now made and it is the gate. The boundary is **code that
executes with kernel privilege in the shipped image** — the source list of the
`cookie_kernel_aarch64_boot` target plus the headers it depends on — so the
measurement is drawn by the build rather than by judgement, and cannot drift
from what is actually trusted. Four categories are capped separately so lines
cannot be laundered between them:

| Category | Lines |
| --- | --- |
| core — privileged portable runtime | 3,427 |
| machine — the AArch64 port, including the physical ledger's reservation table | 3,015 |
| discovery — FDT, inventory, GICv3 topology, timer discovery, boot memory | 1,272 |
| entry — reset vector, freestanding memory, interrupt syscall decode, device IRQ routing, the M7.9 end-to-end proof, the decoded fault reporter | 1,122 |
| **total** | **8,836** |

`core` is the figure comparable to QNX's 605 — but only measured QNX's own way, by semicolons, and only for a kernel of the same scope. Both corrections landed 2026-08-14 (`docs/REFERENCE_NOTES_2026_08_14_QNX.md`): `core` is 1,586 semicolons, **2.6×** the 605, and QNX's microkernel excludes memory management entirely — it lives in `Proc`, a user-space resource manager of 3,924 semicolons. Neither correction moved a ceiling. Boot-time
discovery is counted rather than excused: it runs at EL1 with translation off
against a firmware-supplied blob, so a defect in it is a defect in the most
privileged code on the machine, and excluding it would have shed 723 lines by
relabelling at the time the gate was set. The one exclusion, `machine_host.*`,
is a test double that never enters the image, and it is listed by name rather
than silently dropped.

The ceiling moved once already, by M7.5e's unmap/TLBI work, again by M7.5f:
1,151 → 1,362 in `machine` for EL0 entry/exit assembly and the guard-
page/W^X machinery needed to hand control to EL0 at all; 723 → 996 in
`discovery` for bounded GICv3 topology discovery, needed before an interrupt
can be routed to an EL0 handler; 352 → 362 in `entry` for validating the
guarded EL0 context before `eret`. And again by M7.5g, the first milestone
that delivers a real hardware interrupt to EL0 and returns from it:
1,362 → 1,545 in `machine` for GICv3 redistributor/CPU-interface programming
and the lower-EL IRQ entry/return path; 996 → 1,217 in `discovery` for
bounded DT discovery of the architected timer PPI; 362 → 411 in `entry` for
wiring interrupt admission and the timer into the boot sequence. A fourth
raise followed within the same milestone, not from new capability but from
fixing a defect the M7.5g reland surfaced: `gic_v3_discovery.cpp` failed its
*entire* device-tree walk whenever any unrelated sibling node (real QEMU virt
always has one — `/cpus` declares `#size-cells = <0>`) used a cells value
outside what the parser could represent, producing a 100%-reproducible
silent boot hang with a completely empty serial log. `hardware_inventory.cpp`
had already carried the fix for this exact class of defect since M7.5d;
this walker hadn't inherited it. discovery 1,217 → 1,221, total 4,453 →
4,457. A fifth raise, by M7.5h, is what makes the kernel preemptive rather
than cooperative: 1,280 → 1,360 in `core` for deadline scheduling authority,
alongside the scheduler it extends; 1,545 → 1,766 in `machine` for AArch64
exception-frame decoding of an interrupted lower-EL context and the
preemption path that switches away from it. A kernel that can enter EL0 and
deliver a timer IRQ to it (M7.5g) but cannot preempt what it interrupted
still only runs one process cooperatively. total 4,457 → 4,758. A sixth
raise, by M7.5i, is what makes an address space something the kernel can
retire and reissue rather than a single fixed thing the boot image sets up
once: 1,360 → 1,674 in `core` for generation-bound epochs and process
translation as portable policy, alongside the scheduler they now cooperate
with; 1,766 → 2,174 in `machine` for ASID assignment, execution-universe
composition and translation-root sealing; 411 → 569 in `entry` for wiring
two real EL0 processes through that machinery at boot, which is what proves
generation-bound epochs against something other than a host test. total
4,758 → 5,638. A seventh raise, not from a milestone but from a defect fix,
followed: entry 569 → 570, total 5,638 → 5,639. M7.5i's own boot proof was
flaky under QEMU TCG on shared CI — `Scheduler::choose()` correctly charges
all elapsed real time since the last decision even while uncontested (the
anti-gaming property that stops a thread dodging its charge by avoiding
decision points, which stays exactly as it is), and two EL0/EL1 round trips
plus a UART print were measured exceeding 2ms of guest-visible time —
kernel-internal servicing cost, not the user thread's own work, and
microseconds on real hardware — exhausting process A's round-robin slice
before the deliberate contention test ever ran. Not a scheduler defect —
fixed by passing the still-current since-`start()` timestamp to the
contention-detecting `reschedule()` call instead of a fresh clock read;
`machine_set_timer()` reads the real hardware counter internally, so the
deadline it arms stays correct relative to actual elapsed time regardless.
An eighth raise, by M7.6a, is Cookie's own native IPC replacing the
Linux-substrate transport M8 exists to retire: 1,674 → 2,872 in `core` for
capability-addressed endpoints, continuations and the reply-seal syscall
surface; 2,174 → 2,661 in `machine` for the AArch64 side of that surface
plus bounded user-memory copy with an explicit fault guard — a syscall
copying from an untrusted pointer without one is the kernel analogue of the
cache-attack class this project already refuses to ship AES with; 570 → 751
in `entry` for wiring it through boot. Reply seals exist so a capability
that answered one request cannot be replayed against a second it was never
granted for — the most exposed surface to unprivileged callers landed so
far. total 5,639 → 7,505. A ninth raise, by M7.7, followed: machine
2,661 → 2,757, entry 751 → 826, total 7,505 → 7,676. The `KernelTranslationDomain`
contract this milestone names is excluded from the count entirely (`not_kernel`,
by name) because nothing in the boot image includes it yet — its own comments
say construction, sealing and hardware activation stay separate transactions
until a later milestone splices it in. What's counted is the machinery it is
written against: `Stage1Region` (lower/upper) on the page-table builder, so a
kernel-region root can refuse `map_user_page()` the way its own error code
already implied; and named per-stage failure diagnostics replacing bare
`halt()` at roughly forty boot stop points, so a failure reads as which stage
rejected the machine rather than as an opaque QEMU timeout. A tenth raise, by
M7.8, closes the gap M7.6a's own IPC surface opened: core 3,052, machine
2,772, discovery 1,234, entry 838, total 7,676 → 7,896. A capability minted
for a numeric `ThreadId` was holder-checked by that same bare number, so a
recycled thread ID — the ordinary result of a process exiting and a new one
being admitted — could let the new process silently inherit whatever the old
one could still reach. `execution_authority.hpp` binds a capability's holder
to `ExecutionAuthority` — thread plus address-space epoch identity, not
thread alone — so a recycled ASID with a fresh generation is a different
holder even at the same `ThreadId`. The migration is deliberately
fail-closed: bound capabilities are unusable through the legacy
`ThreadId`-only IPC path rather than silently losing their generation
binding, until M7.8.2 adds the explicit `ExecutionAuthority` IPC path — the
finer-grained M7.8.1–M7.8.4 breakdown this number comes from is
`docs/ROADMAP_TO_COMPLETION.md`, superseded by this document but kept as a
historical record of that breakdown; its own M7.8.3 (trusted syscall
identity) looks likely already satisfied by the shipped
`cookie_kernel_syscall_entry`, which resolves the caller from
`PreemptionCoordinator::running()` rather than a user register, but that is
unconfirmed by a dedicated test and stated there rather than claimed here.
Do not read anything else in that file as current — its own "M7.9" names a
different milestone than this document's. An
eleventh raise, by M7.9's first increment, closes a gap M6.0 and M7.1 both
named and deferred: core 3,052 → 3,117, total 7,896 → 7,961.
`interrupt_attach`, `interrupt_detach` and `interrupt_complete` are now
capability-checked at the `Kernel` composition layer — `InterruptTable`
itself is unchanged and still does not consult `CapabilityTable`; the check
lives beside `ipc_send`/`ipc_receive`'s own, re-validated fresh on every call
so a capability revoked between attach and complete cannot still authorize
either. A twelfth raise, by M7.9's second increment, closes a third named
gap: entry 838 → 867, total 7,961 → 7,990. `cookie_kernel_syscall_entry`
decodes and dispatches all three interrupt syscalls, failing closed on any
`Result` error exactly as the existing send/receive/reply dispatch already
does, and returns `interrupt_complete`'s must-service-again answer in `x0`
the way a completed receive already returns its byte count there. No caller
exercises the path yet — the driver process that will is the remaining gap.
A thirteenth raise, by M7.9's third increment, closes a second: machine
2,772 → 2,790, discovery 1,234 → 1,272, entry 867 → 892, total
7,990 → 8,071. `discover_architected_timer` now also decodes the same DTB
node's virtual-timer entry — reused as M7.9's stand-in device source rather
than building a UART or virtio-mmio driver just to prove the capability path,
the design doc's own recorded answer to which source the first proof uses
(discovery). `initialize_gic_v3_primary_cpu` configures both PPIs identically
but leaves the device PPI disabled until something attaches to it, and a new
`gic_v3_set_ppi_masked` moves it between enabled and disabled at every
transition after that (machine). `cookie_aarch64_irq_dispatch` gains a second
branch, structurally parallel to the timer's, that routes the device PPI
through `Kernel::dispatch_interrupt()` instead of `PreemptionCoordinator` and
masks unconditionally afterward — `InterruptTable` has no slot to ask about
for a spurious assertion, and leaving an asserting line enabled with nobody
to charge it to is a livelock at the controller regardless of whose fault it
is; the same masking now extends to the interrupt syscall handlers the
twelfth raise landed, which decoded the three calls but did not yet touch
GIC state (entry). No caller exercises any of this yet — the driver process
that will is M7.9's last gap. A fourteenth raise closed something review
found rather than a milestone gap: core 3,223, machine 2,821, entry 896,
total 8,212. `docs/M7_1_INTERRUPT.md` and `docs/M7_9_USER_SPACE_DRIVERS.md`
both say `begin_service` is deliberately not a syscall because "the count
rides back on the wakeup" — but nothing in the tree ever called it or
delivered its result anywhere, which meant `interrupt_complete` could never
have succeeded outside a test that skips straight to it: nothing moved a
source from pending to in_service. `InterruptDeliveryTable` (core) is a
one-slot-per-thread handoff mirroring `IpcContinuationTable`'s own shape;
`Kernel::dispatch_interrupt` now calls `begin_service` itself the instant a
driver is woken. `complete_interrupt_current` (machine) is the interrupt
analogue of `complete_ipc_current`, writing the delivery into `x2`/`x3` on
every resume rather than `x0`/`x1`, which `complete_ipc_current` already uses
and a driver that also does IPC would otherwise collide with.
`complete_after_switch` in `aarch64_boot.cpp` now calls both completions on
every switch (entry). Found and closed before M7.9's own boot proof tried to
use `interrupt_complete` and could not have. A fifteenth raise, by M7.9's
fourth and last increment, closes the final named gap: entry 896 → 951, total
8,212 → 8,267. `kernel-arm64-native` now gates on `COOKIE:M7.9:ATTACHED` →
`..:DISPATCHED` → `..:SERVICED` → `..:COMPLETED`. Process A - reused after
its M7.6a role concludes rather than a third address space - attaches to a
capability minted at boot, arms the reused virtual-timer PPI as the stand-in
device it just attached to, and is redirected by `complete_after_switch` into
calling `interrupt_complete` the instant its delivery arrives, the same
`elr_el1`-rewrite technique the existing send/receive redirects already use
rather than a polling loop the driver's own program would have to run. None
of the new entry points needed anything baked into their own bytes beyond a
bare `svc` - the same shape word 8's send redirect already proved - so
nothing in this increment required an instruction pattern this tree had not
already run under CI. A sixteenth raise, by M7.8.2's first increment, closes
part of a gap `ipc_endpoint.hpp`'s own comment already named: core
3,223 → 3,327, total 8,267 → 8,371. A capability minted via
`CapabilityTable::mint(ExecutionAuthority, ...)` could be described and
revoked but never actually used to send or receive - the ThreadId-only IPC
path fails closed for it by design, and nothing existed on the other side of
that failure. `IpcEndpointTable::send`/`receive` gain `ExecutionAuthority`
overloads, sharing the rendezvous/pending-slot/reply-seal mechanics with the
existing `ThreadId` overloads through two new private helpers rather than
duplicating them, and `Kernel::ipc_send`/`ipc_receive` gain matching
overloads so the new path is reachable from the same composition layer
M7.9's `interrupt_attach` already lives at. Partial, and documented as such
in the same diff: pending calls, reply seals and completed replies are still
recorded and looked up by bare `ThreadId`, so a capability check happens at
send/receive but nothing yet re-checks that the caller collecting a reply is
still the generation the transaction was established under - M7.8.2's
remaining gap, not this raise's. A seventeenth raise, by M7.8.2's second
increment, closes it: core 3,327 → 3,419, total 8,371 → 8,463.
`IpcReplySeal`, the private `PendingSlot` and the private `CompletedSlot`
each gain an `AddressSpaceIdentity` field, populated only when the
corresponding side used the `ExecutionAuthority` send/receive overload -
additive, so a default-constructed, invalid identity means "the legacy path
was used here, nothing to compare," not "untrusted." `reply`,
`reply_transaction` and `take_reply` gain matching `ExecutionAuthority`
overloads that check the recorded generation before delegating to the
existing `ThreadId` implementation, refusing a same-thread-different-
generation caller or server with the exact code a wrong thread already
gets — not a distinguishing one, so a stale generation cannot learn "right
thread, wrong incarnation" from the failure alone. None of it is
discretionary — see
`.github/scripts/kernel-line-count.sh` for the full justification recorded
beside each raise.

The ceilings are the measured values, not the aspiration — a ratchet. A gate set
at 605 would be red on arrival and switched off within a week; one set
comfortably above the count would measure nothing. The kernel is free to shrink
and free to be rewritten, and cannot grow without someone editing the number and
defending it in review. The open M7.5e–M7.8 stack will therefore trip this gate
as it lands, which is intended: the growth and the decision to permit it should
be one reviewable diff.

**Exit criteria:** Cookie Kernel boots on QEMU virt, schedules multiple EL0
processes, delivers timer and device interrupts to user-space handlers, passes
the full EMNL wire-format and capability fuzz corpus, and reports a trusted line
count under the declared ceiling.

All five are met. That closes the checklist, not the phase: Phase 2's own
opening line sets a bigger bar than these five bullets ever measured - "finish
the AArch64 kernel until it can run Cookie's own services" - and nothing here
runs a Cookie service. Two EL0 processes proving IPC, preemption and interrupt
mechanisms are not services; they are the mechanisms services will need. What
these five bullets actually gate is that the mechanisms are real and CI-proven
rather than claimed, which is worth having exactly because it is narrower than
"self-hosting" and can be checked by a machine instead of asserted in prose.
Taken in order:

- *Boots on QEMU virt* — **met**, and gated: `kernel-arm64-native` greps the
  serial log for both markers and fails the workflow without them.
- *Schedules multiple EL0 processes* — **met.** Two isolated EL0 processes run
  under `main` today, gated by `kernel-arm64-native`: process A and process B
  each get their own sealed translation root, ASID and address-space epoch,
  communicate over Cookie's own native IPC with reply seals (M7.6a), and are
  preempted between each other by a real GICv3 timer interrupt, proven by the
  `COOKIE:M7.5i:CONTENTION_ARMED` → `A_TO_B` → `B_TO_A` → `ISOLATED_A_B_A`
  marker sequence. `cookie_kernel_syscall_entry` no longer panics on every
  lower-EL syscall; it decodes and dispatches real ones.
- *Delivers timer and device interrupts to user-space handlers* — **met.**
  The timer half was already real: GICv3 timer IRQs are delivered to EL1,
  correctly attributed, and drive the preemption above (M7.5g). The device
  half is M7.9, now landed end to end and gated by `kernel-arm64-native`:
  capability-gated `interrupt_attach`/`interrupt_detach`/`interrupt_complete`
  at the `Kernel` composition layer; `cookie_kernel_syscall_entry` decoding
  all three from EL0; `cookie_aarch64_irq_dispatch` routing the discovered
  virtual-timer PPI, reused as a stand-in device source, through
  `Kernel::dispatch_interrupt()` with mask-until-complete enforced at the
  controller; `begin_service`'s result delivered to the driver on its own
  resume without a syscall, the gap review found while preparing this proof
  (`InterruptDeliveryTable`, `complete_interrupt_current`); and the proof
  itself - process A, reused after its M7.6a role concludes, attaches to a
  real capability, the reused PPI is armed, and `complete_after_switch`
  redirects A straight into `interrupt_complete` the instant its delivery
  arrives, observed via `COOKIE:M7.9:ATTACHED` → `..:DISPATCHED` →
  `..:SERVICED` → `..:COMPLETED` in that order.
- *Passes the full EMNL wire-format and capability fuzz corpus* — **met.**
  `fuzz/ipc/` fuzzes `os::ipc` — the service-layer RPC decoder used over the
  Linux substrate's `SOCK_SEQPACKET` transport. `os::kernel`'s own surface
  needed its own coverage, since a well-formed RPC envelope proves nothing
  about a hostile register value at the syscall boundary: `fuzz/kernel/
  ipc_syscall_fuzz.cpp` now exercises `decode_ipc_send_syscall`/
  `decode_ipc_receive_syscall`/`decode_ipc_reply_syscall` directly against raw
  register values, and `fuzz/kernel/capability_fuzz.cpp` runs a bounded script
  of mint/grant/revoke/describe/holds operations - both the legacy
  `ThreadId`-only surface and M7.8's `ExecutionAuthority`-bound one - against
  a single `CapabilityTable`, reusing previously-minted ids so the
  transitions between operations (derive past the depth limit, revoke racing
  a grant, describe after teardown) are what gets exercised, not just each
  call's own argument validation in isolation. Both are smoke-tested every
  push (`fuzz-smoke` in `ci.yml`) and fuzzed nightly for real wall-clock time
  (`fuzz-nightly.yml`), the same discipline every other target in the
  directory already gets.
- *Reports a trusted line count under the declared ceiling* — **met**, and has
  been since M7.10 landed. The ceiling moved seventeen times across this stack —
  once per milestone plus three defect fixes — each raise landing in the same diff
  as the lines it covers, with the justification recorded in
  `.github/scripts/kernel-line-count.sh` and restated in
  `docs/M7_10_LINE_COUNT.md`. The gate has never gone more than one commit
  without being either green or in the process of an honest, reviewed raise.

**Honest position:** an unaudited new kernel is genuinely worse than the mature
one it replaces, for as long as the intermediate state lasts. That cost is
accepted deliberately and should stay visible.

---

## Phase 2b — Memory as a first-class object (M7.11)

Design document: `docs/M7_11_MEMORY.md`, which makes the decisions listed
below rather than restating them — in particular the one that cannot be
revisited later, that **the kernel has no dynamic allocator**.

Added because a gap review found it missing rather than deferred, and it
blocks Phase 3 whether or not it is written down. Everything M7 built —
address spaces, epochs, capabilities, IPC, interrupts — assumes memory that
was mapped once at boot from a plan computed before any process existed.
There is no way to create an address space after boot, no way to grant or
revoke a mapping, no fault path that resolves anything, and no allocator
that outlives `plan_early_boot_memory`. The two identity maps this phase's
own pre-MMU work added are the whole of Cookie's virtual memory story.

Phase 3 cannot start without this: a service that restarts (the M2.10
fixture, Phase 3's exit criterion) needs its address space torn down and
rebuilt, which is precisely the operation that does not exist.

- **A physical page allocator that is not a boot-time bump arena.** `EarlyPageArena`
  hands out pages monotonically and never reclaims; page-table pages are
  explicitly documented as never freed. Both are correct for boot and neither
  survives a process exiting.
- **Address-space lifecycle**: create, map, unmap, revoke, destroy, as
  capability-gated operations rather than boot-routine calls, reusing the
  generation-bound epoch machinery M7.5i/M7.8 already built so a stale
  reference to a destroyed space fails closed the way a stale capability does.
- **A fault path that resolves rather than panics.** Today an EL0 data abort
  is a kill; demand paging, stack growth and copy-on-write all need a
  translation fault to be a question the kernel can answer.
- **The kernel-heap decision, made explicitly and before anything allocates.**
  The alternative Cookie should weigh seriously is *no kernel dynamic
  allocation at all*: kernel objects derived from memory a process already
  holds authority over, with fixed sizes, so that a syscall can never fail for
  want of kernel memory and per-process memory quota is structural rather than
  accounted. The cost is real — userland must run an explicit memory manager
  and the API is harder to use. The reason to decide it now is that it cannot
  be retrofitted: a kernel heap makes every user-triggerable allocation a
  denial-of-service channel and a cross-process side channel, and by the time
  that is visible, every subsystem depends on it.
- **Policy stays out of the kernel.** The kernel should expose the primitives
  to manipulate translation structures; where a process's regions *go* is a
  userland decision. This is the same argument M7.0 already made for keeping
  transport out of the kernel, applied to layout.

**Exit criteria:** a process is created, runs, faults, has the fault resolved,
exits, and every page it held is reused by a later process — proven under
`kernel-arm64-native`, not on the host. The M7.10 line count is reported and
its raise justified in the same diff, as every M7 milestone has been.

**Ordering hazard, recorded here because it is cheap now and expensive later:**
Phase 4 lists IOMMU programming under hardware bring-up, which is the right
place for the *programming* and the wrong place for the *decision*. DMA
isolation has to exist before the first DMA-capable driver, not after: a
driver written against physical addresses cannot be retrofitted behind an
SMMU without being rewritten, and an identity/bypass window opened "for now"
at boot is the kind of thing that is never closed. The rule Cookie should
adopt is that a device capability and its stream mapping are granted
together or not at all — there is no interval during which a device can DMA
and is not confined. That rule costs nothing to state today and cannot be
imposed once drivers exist.

---

## Phase 3 — Substrate cutover (M8)

Move the service layer off Linux and onto the Cookie Kernel. This is the phase
the project has been building toward since M7.0 and the one most likely to be
underestimated.

- Replace `SOCK_SEQPACKET` + `SCM_RIGHTS` + `SCM_CREDENTIALS` with native
  capability-addressed IPC. The service layer was written against typed bounded
  messages and brokered capabilities, so the *model* transfers; the transport
  does not.
- Retire the substrate probe work in M6.3, which inspects a Linux kernel for
  facilities Cookie now implements itself.
- Re-establish every existing gate against the new substrate. A gate that only
  ever ran on Linux proves nothing about Cookie.
- Storage service over a real block device rather than a host filesystem.

**Exit criteria:** the M2.10 restart fixture — a client surviving a service
restarting underneath it — passes on Cookie Kernel with no Linux in the image.

**Honest position:** this is where the schedule risk concentrates. Every
optimistic estimate in this document is most likely to be wrong here.

---

## Phase 4 — Hardware bring-up (M9)

First physical board. Target selection is deliberately deferred to this point
because it should be driven by what the kernel needs (documented GICv3, a
tractable IOMMU, an accessible secure world) rather than by what is on a desk.

- Board support: clocks, UART, timer, interrupt controller, storage controller.
- Display path to real panel through the M3 compositor, with no application
  reaching DRM/KMS or a device node.
- Touch input into the M3.2 input routing.
- **IOMMU programming**, which turns `DmaCapability::iommu_confined` from a
  recorded claim into an enforced one. M6.0 explicitly refuses to call a driver
  confined when the platform cannot confine it; this is where the platform can.
- Power management: suspend, resume, and the idle-wakeup budget the vision
  document requires.

**Exit criteria:** Cookie boots from power-on to a usable shell on a physical
phone-class device, and the M4.2 resource budgets are measured on it rather than
on a host.

---

## Phase 5 — Production trust anchors (M10)

Everything currently backed by a test-only provider becomes real.

- TEE client path and a production key provider replacing the OpenSSL test
  provider and its fixed wrapping root.
- Hardware monotonic counter for `MonotonicSecurityState`. Until this exists the
  anti-rollback claim is an interface, and `AGENTS.md` already forbids claiming
  otherwise.
- Verified boot chain that terminates in a measured root filesystem and actually
  produces the evidence M5.0 designed.
- Secure element for credential and profile-root protection.
- Signed update and rollback with recovery.

**Exit criteria:** no security claim in the tree depends on a provider marked
test-only. Anti-rollback survives a physical reflash attempt.

**Honest position:** the AES implementation must reach this phase free of
secret-indexed lookup tables and using the platform's cryptographic
instructions. `AGENTS.md` records that the table-driven construction is exactly
what the cache-attack references recover keys from. It also records an open gap:
`AeadTag` and `AeadNonce` expose raw `std::array` members, so a variable-time
`==` still compiles. That gets closed by construction here, not by a rule.

---

## Phase 6 — Radio and telephony (M11)

The largest remaining attack surface and the one the architecture has said least
about.

- Baseband boundary: the modem is treated as a hostile peripheral behind an
  IOMMU, not a trusted co-processor. This is the single most important design
  decision in the phase.
- Cellular, Wi-Fi and Bluetooth as user-space drivers under device access policy.
- Telephony service with the same typed bounded surface as Storage and Keys.
- Network stack as a service, per M7.0's refusal to put transport in the kernel.

**Exit criteria:** a call and a data session on a physical device, with the
modem unable to reach memory the policy did not grant it.

---

## Phase 7 — Application platform (M12)

- Public SDK: the typed semantic surfaces of Storage, Keys, UI, telephony.
- Application signing, distribution and permission model, building on M1.
- First-party applications: dialler, messages, contacts, settings, camera.
- Developer documentation of the invariants, which the tree already writes well.

**Exit criteria:** a third party can build, sign, install and run an application
without any platform-private header.

---

## Phase 8 — Production readiness (M13)

- Performance and power budgets from `PROJECT_VISION.md` enforced as gates:
  cold boot, resume, input-to-present, frame deadlines, idle wakeups, background
  CPU, resident memory, radio wakeups, I/O amplification.
- External security audit, with the kernel's small size as the thing that makes
  an audit affordable — the argument M7.0 rests the entire decision on.
- Certification, if a target is chosen. `PROJECT_VISION.md` correctly separates
  historical FIPS/NIST references from present-day compliance claims.
- Long-term maintenance and update policy.

---

## Rules this roadmap does not get to break

Restated because a roadmap is exactly the document that starts eroding them:

- References teach principles; Cookie determines implementation.
- A phase is not complete because it functions. Security, boundedness, power and
  accessibility are exit criteria, not follow-up work.
- No milestone claims a hardware-backed property while backed by a test provider.
- The kernel stays small enough to read completely, and a gate enforces it.
- When priorities conflict, the security boundary is preserved first and cost is
  reduced by better design rather than by bypassing the boundary.

## Estimation

Deliberately absent. `docs/M7_0_KERNEL.md` calls the kernel direction a
multi-year one, and every phase after Phase 3 depends on hardware that has not
been selected. Dates here would be invented, and this project's documentation
has so far been notable for not inventing things.

What can be stated, now with one measurement behind it: Phase 0 was estimated at
days rather than weeks because the defects were mechanical and the CI was
comprehensive, and it took one. That is the only estimate in this document that
has been tested, and it was the easiest one to make.

The rest is unchanged and should be read more cautiously for it. Phase 1's code
is merged and none of its exit criteria are met, which is a different kind of
remaining work from integration — closing the Storage seam, paying or cutting
the coercion-resistance debt, and building a network path that the admission
policy can govern. Phase 2's M7.5e–M7.8 stack is landed; M7.9 and M7.10 are
both done. Phase 3 onward is genuine new engineering.
