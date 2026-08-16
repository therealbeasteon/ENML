# Security audit — 2026-08-16

A pass over the Cookie Kernel looking for defects rather than for missing
features, driven by one question: **what does real hardware require that the
emulator does not?** Every gate this project has runs on
`qemu-system-aarch64 -machine virt`, so anything the emulator is lenient about
is invisible, and anything it is *stricter* about is equally invisible in the
other direction.

Five defects were found. Four are fixed; one is recorded with its fix deferred
and its reason. Sections at the end record what was examined and found **sound**,
because an audit that only lists problems does not tell you what was covered.

## Findings

### 1. Every TLB entry was global — cross-process memory disclosure

**Severity: high. Fixed (PR #138).**

`install_process_translation` switches `TTBR0_EL1` without flushing, which is
correct and is what ASIDs are for. It requires entries to be *tagged*, which
requires `nG` in each leaf descriptor. **No descriptor in the tree set `nG`.**

Every Cookie process maps its code at `user_code_virtual` and its stack at
`user_stack_virtual` — the same virtual addresses. On silicon, a process switch
would leave the outgoing process's entries live and matching exactly where the
incoming one reads. It also emptied `retire_process_asid`: `tlbi aside1is`
matches tagged entries, and a global entry carries no tag, so a destroyed
space's translations survived the invalidation M7.11's two-phase destroy is
built on.

QEMU TCG flushes its software TLB when `TTBR0_EL1` is written, so the fast path
was never taken and every gate stayed green.

### 2. No cache maintenance existed — stale instruction execution

**Severity: high. Fixed (PR #139).**

Boot writes A64 words through a writable mapping and then executes them. ARMv8
puts the clean-to-PoU and I-cache-invalidate obligation on the writer.
`dc`, `ic`, `dcache`, `icache`: zero hits in `core/oskernel`.

QEMU has no separate instruction cache. On any phone, the fetch may read
whatever was in that line before. Fixed by `aarch64_publish_instructions`,
which reads `CTR_EL0` for line sizes rather than assuming 64 bytes — a stride
*larger* than the implementation's line skips lines silently and looks like it
ran.

### 3. `CPACR_EL1` was never written — cross-process vector register leakage

**Severity: high. Fixed (PR #141).**

`aarch64_exception.hpp` already stated the hazard precisely:

> Silently letting userspace touch V0-V31 without preserving them would create
> cross-thread information leakage.

**Nothing enforced it.** `CPACR_EL1` appeared nowhere in the tree, and its
`FPEN` field
[resets to an architecturally UNKNOWN value](https://developer.arm.com/documentation/111107/2026-03/AArch64-Registers/CPACR-EL1--Architectural-Feature-Access-Control-Register).
Firmware that used floating point itself routinely leaves it enabled. Cookie
never saves or restores V0-V31 across a context switch, so a process could have
left secrets in the vector register file for the next process to read.

Fixed by trapping FP, SVE and SME at EL0 and EL1. **Trapped rather than saved**,
because trapping is the honest state for a kernel with no FP context-switch
policy: the choice is between refusing the registers and sharing them.

This one is the inverse of findings 1 and 2 — QEMU resets `CPACR_EL1` to a value
that traps, so the *emulator was stricter than the hardware* and hid the gap
just as effectively.

### 4. `SCTLR_EL1` was inherited — EL0 held a cache side-channel toolkit

**Severity: medium. Fixed (PR #141).**

`SCTLR_EL1` was read-modify-written: three bits set, every other bit left at
whatever firmware left. Inherited that way were EL0 endianness, stack-alignment
checking, hardware write-implies-XN, whether EL0 may execute cache maintenance
(`UCI`), read cache geometry (`UCT`) or zero a cache line (`DZE`), and whether
EL0 may touch DAIF (`UMA`).

`DC CVAU` / `IC IVAU` / `CTR_EL0` / `DC ZVA` are the standard Flush+Reload
construction kit on ARM, and
`docs/REFERENCE_NOTES_2026_08_16_CACHE_CHANNELS.md` already describes that
attack. Cookie was granting them to userland *by inheritance rather than by
decision* — a channel below page granularity, which is exactly the one
`docs/M7_11_FAULT_PRIVACY.md` admits it cannot close in software.

Now denied, along with `UMA`, and `WXN` set so W^X is a property of the
translation regime rather than a rule the mapping code remembers.

**An honest caveat.** Linux traps EL0 cache maintenance and *emulates* it, for
errata reasons. Cookie denies it outright, which is cheap only because Cookie
has no userland. If a future userland needs `IC IVAU` — a JIT would — the answer
must be trap-and-emulate under a policy, not re-enabling the bit. Re-enabling it
would restore the channel silently.

### 5. An unchecked length sizing a span over a fixed buffer

**Severity: low (not currently reachable). Fixed in this change.**

`verify_user_bytes` in the boot proof builds
`std::span{actual.data(), expected.size()}` over a fixed
`max_ipc_inline_bytes` array without checking `expected.size()` against it.
Every caller passes a small literal, so it cannot fire today. It is a kernel
stack overflow one caller away, and the caller that introduces it will not be
looking at this function.

### 6. The fuzz corpus had stopped tracking the kernel

**Severity: process, not code. Fixed in this change.**

`fuzz/kernel/ipc_syscall_fuzz.cpp` covers three syscall decoders. By the time
M7.11 and M7.12 landed there were eight. The five added since — `decode_call`
itself, both address-space decoders, the pager's answer, and thread creation —
had none, while being reachable from EL0 with attacker-chosen 64-bit register
values.

`fuzz/kernel/syscall_surface_fuzz.cpp` covers the rest, including `decode_call`
over the whole 16-bit space rather than only values a caller "should" send.

## Deferred, with reasons

- ~~**`PSTATE.PAN`.**~~ **Taken.** `SCTLR_EL1.SPAN` is now cleared when
  `ID_AA64MMFR1_EL1` says the core implements `FEAT_PAN`, so PAN is set on every
  exception entry and an ordinary EL1 load or store to any page EL0 can reach
  faults; only `ldtr`/`sttr` succeed, which is exactly what Cookie's user-copy
  path already used. The feature check is not optional — on a core without
  `FEAT_PAN` that bit is RES1 and clearing it is CONSTRAINED UNPREDICTABLE.
  Both halves are gated: the `cortex-a72` run proves the feature-absent path
  (`COOKIE:M7.13:NO_PAN`) and the EL2 run moved to `-cpu max` so one run
  actually executes with PAN set (`COOKIE:M7.13:PAN`). That second run is the
  one that fails if any kernel path still reaches user memory with an ordinary
  access.
- **A relocatable image, GICv2, a second UART, SMP.** All in
  `docs/M7_13_HARDWARE_NEUTRALITY.md`, all work rather than defects.

## Examined and found sound

Listed because coverage is part of an audit's result.

- **Exception frame save/restore.** All 31 GPRs plus `SP_EL0`, `ELR_EL1` and
  `SPSR_EL1` are saved and restored; `ESR_EL1` and `FAR_EL1` are captured but
  never written back to hardware, so the faulting address cannot reach EL0
  through the restore path. This matters because `docs/M7_11_FAULT_PRIVACY.md`
  is built on the kernel not disclosing `FAR`.
- **First entry into a new thread.** `admit_frame` takes a value-initialised
  `ExceptionFrame`, so a new thread starts with every general register zero and
  inherits nothing. `cookie_aarch64_enter_el0` zeroes x0–x30 before `eret` for
  the same reason.
- **The syscall number decode.** `decode_call` searches the table rather than
  indexing it, so an attacker-chosen call number is never an array subscript —
  no Spectre-v1 gadget on the most attacker-reachable path, and the code says
  that is why.
- **Range arithmetic.** The overflow guards in `reserve_physical`,
  `aarch64_publish_instructions` and `MemoryGrant::contains` are wrap-safe in
  the `base > MAX - (length - 1)` form rather than the `base + length` form
  that wraps.
- **TLB maintenance scope.** Unmap uses `tlbi vaae1is` per page and ASID
  retirement uses `tlbi aside1is` — correct, and correct *now* that finding 1 is
  fixed. `vaae1is` is broader than necessary once entries are ASID-tagged;
  narrowing it to `vae1is` is an optimisation, not a correction.
- **Capability lookup.** Linear over 256 slots. Deliberately not turned into an
  indexed lookup: folding the slot into the identifier — the trick
  `address_space_object_id` uses for generations — would make a capability
  identifier an **occupancy oracle** over a shared kernel table. Mint twice,
  compare, learn how full it was. Exception entry and exit dominate 256 cached
  comparisons anyway, so this trades a disclosure for unmeasured speed.

## Not examined

Stated so the coverage claim is not read as wider than it is.

- **Speculation beyond the syscall decode path.** Branch-target injection,
  speculative store bypass and the SMCCC workaround interfaces were not
  assessed. Cookie has no mitigation for any of them and no `csdb`/`sb` usage.
- **Memory ordering around device registers under an out-of-order core.** The
  GICv3 and UART paths use `isb` where the architecture requires it for system
  register side effects, but the ordering of MMIO against normal memory was not
  systematically checked.
- **The service layer above the kernel.** This pass was the kernel only.
- **`docs/M4_5_FUZZING_DEPTH.md`'s corpora other than the kernel's.**

## Context from outside the project

seL4 completed functional correctness on AArch64 in April 2024 and integrity in
April 2025, and as of mid-2026 **time protection remains explicitly outside its
proofs and is described as an unsolved research problem**. That is worth holding
next to finding 4: the channel Cookie closed by denying EL0 cache maintenance is
a *primitive* for the class of attack that the most heavily verified microkernel
in existence still cannot prove the absence of. Denying the primitive is not the
same as closing the channel, and this document should not be read as claiming
otherwise.

## The pattern worth carrying forward

Three of the six findings are the same defect: **state Cookie inherited instead
of establishing.** `nG` unset, `CPACR_EL1` unwritten, `SCTLR_EL1`
read-modify-written. In each case the kernel had a correct design, complete
code, and an accurate comment describing the property — and the property did not
hold, because the register that decides it was never written.

The rule that falls out: **a machine-control register the kernel depends on must
be written in full, not adjusted.** If Cookie's behaviour depends on a bit, that
bit is Cookie's to set, including the ones it wants left at zero.

Sources:
[CPACR_EL1](https://developer.arm.com/documentation/111107/2026-03/AArch64-Registers/CPACR-EL1--Architectural-Feature-Access-Control-Register),
[SCTLR_EL1](https://developer.arm.com/documentation/ddi0595/2021-06/AArch64-Registers/SCTLR-EL1--System-Control-Register--EL1-),
[DC CVAU](https://developer.arm.com/documentation/ddi0601/2025-03/AArch64-Instructions/DC-CVAU--Data-or-unified-Cache-line-Clean-by-VA-to-PoU),
[seL4 news](https://sel4.systems/news/).
