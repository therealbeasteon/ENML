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

Measured on `main` at `19be516`, not claimed:

| Layer | State |
| --- | --- |
| Build, IPC, OSIDL, supervisor, sandbox (M0) | Complete, frozen gate |
| Package and application lifecycle (M1) | Complete |
| Storage, keys, broker, runtime session (M2) | Complete |
| Display, compositor, semantic UI (M3) | Complete |
| Trusted phone shell and product security (M4) | Through M4.10g merged; **13 PRs open** |
| Verified boot evidence (M5) | Designed and tested; nothing produces the evidence |
| Device access, time protection (M6) | Policy complete; **no platform enforces it** |
| Cookie Kernel (M7) | Through M7.5a merged; **9 PRs open** |

Roughly 40,000 lines of implementation against 25,000 lines of test. Zero
`TODO`/`FIXME`/`HACK` markers in the tree. Twelve CI workflows, all green on
`main`. The discipline is real and the roadmap should not spend it.

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

## Phase 0 — Stabilise: land the backlog *(current)*

Twenty-seven open draft PRs in three stacked chains plus one orphan. Every
stack is blocked at its base on a small mechanical defect, not a design problem.
Diagnosed:

| Stack | PRs | Blocking defect |
| --- | --- | --- |
| M4.10i → M4.10t (encrypted profile storage) | #34–#45 | `profile_protector.hpp:77` uses `os::core::ErrorDomain::boot`, which does not exist. The four existing `osboot` sources all use `ErrorDomain::security`. |
| M4.11 → M4.15 (recovery, networking, leases) | #47–#50, #58 | `recovery_policy_test` is registered with CTest but absent from the build-target list in `.github/workflows/m4-shell.yml`, so it reports `Not Run`. |
| M7.5d → M7.8 (AArch64 kernel) | #52–#61 | `kernel-arm64-native` failing at the base of the stack. |
| M4.1 (supervised phone shell) | #28 | None — fully green since 2026-08-10, simply never merged. |

Every stack base is also behind `main` and needs rebasing.

### Stranded merges

Three milestones report as merged on GitHub but are **not in `main`**:

| Milestone | PR | Merged into | Recovered by |
| --- | --- | --- | --- |
| M4.10h authenticated profile storage format | #33 | `m4-10g` | `m4-10i` (PR #34) |
| M7.5b real AArch64 exception entry | #46 | `m7-5` | `m7-5d` (PR #52) |
| M7.5c AArch64 stage-1 translation | #51 | `m7-5b` | `m7-5d` (PR #52) |

The cause is a merge-ordering race, and the M4.10h case shows it precisely: PR
#32 merged `m4-10g` into `main` at 11:58:17, and PR #33 merged `m4-10h` into
`m4-10g` at 11:58:24. Seven seconds decided whether a milestone shipped. The
parent had already left, so the child merged into a branch nothing was going to
read again.

Nothing is lost — every stranded commit survives in the open stack branch above
it, and landing the stacks bottom-up onto `main` recovers all three. But it is
the reason this phase outranks everything else, and it produces a rule worth
keeping: **a closed PR is not evidence that its work is in `main`.** Check
ancestry. `git merge-base --is-ancestor origin/<branch> origin/main` answers it
in one command, and the twelve green workflows on `main` did not, because code
that never arrived cannot fail a gate.

**Order of work:** land #28 first (green, no dependants), then each stack
bottom-up, fixing the base defect and rebasing before pushing the next link.
A stack is never merged out of order to save time; a rebase that skips a link
silently rewrites the link below it.

**Exit criteria**

- Zero open PRs whose only obstacle is a stale base or a missing build target.
- `main` green across all twelve workflows after each stack lands.
- 59 already-merged remote branches deleted; only live work visible.
- `AGENTS.md` and `README.md` describing the tree that exists. Both currently
  stop at M2.10 and point at a merged PR as the active track.

**Honest position:** this phase adds no capability. It converts work that has
already been done into work that can be built on, which is the only reason it
outranks everything below it.

---

## Phase 1 — Complete the M4 product security line

The security features a user actually touches. Most of the code lands in Phase 0;
this phase finishes what the stacks leave open.

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

**Honest position:** duress erasure is only as durable as the storage medium's
erase semantics. Until Phase 4 puts this on real flash with a real controller,
the guarantee is "the key is gone from the abstraction we built", which is not
the same as "the bytes are gone from the die".

---

## Phase 2 — Cookie Kernel to self-hosting

Finish the AArch64 kernel until it can run Cookie's own services. `docs/M7_0_KERNEL.md`
already fixed the order — ABI, host-testable core, machine layer, emulator boot —
and the first three are substantially done.

Remaining, mostly already written and sitting in PRs #52–#61: standalone boot
image, descriptor teardown and TLBI, the first real EL0 process and syscall
return, GICv3 timer delivery, a tickless preemptive scheduler, generation-bound
address-space epochs with ASID quarantine, capability-addressed native IPC with
reply seals, a stable translation domain, and execution authority bound to
address-space generations.

Then the part no PR covers yet:

- **M7.9 — user-space driver framework.** Interrupt handlers inside driver
  processes, connected to a vector by a kernel call, under M6.0 device access
  policy. This is the piece that makes "no drivers in the kernel" true rather
  than aspirational.
- **M7.10 — the line count gate.** A CI check that fails when the kernel exceeds
  its budget. `docs/M7_0_KERNEL.md` states the measure of success is a single
  number and that reaching Linux's size means the exercise failed. A number
  nobody enforces is a wish.

**Exit criteria:** Cookie Kernel boots on QEMU virt, schedules multiple EL0
processes, delivers timer and device interrupts to user-space handlers, passes
the full EMNL wire-format and capability fuzz corpus, and reports a trusted line
count under the declared ceiling.

**Honest position:** an unaudited new kernel is genuinely worse than the mature
one it replaces, for as long as the intermediate state lasts. That cost is
accepted deliberately and should stay visible.

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

What can be stated: Phase 0 is days of work, not weeks, because the defects are
mechanical and the CI is comprehensive. Phase 1 and 2 are mostly written and
awaiting integration. Phase 3 onward is genuine new engineering.
