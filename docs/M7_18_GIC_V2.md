# M7.18 — What the kernel needs from an interrupt controller

**Status: design, plus one fix that could not wait.** Everything below the
"GICv2" heading is decided and not yet built — the controller struct, the
discovery seam and the v2 register programming are the milestone this document
exists to specify.

One finding in it *is* fixed, in the diff that lands this file, because it is
fatal rather than absent: the EL2 drop wrote `ICC_SRE_EL2` unconditionally, and
that instruction is UNDEFINED on a GICv2-only SoC. See "the EL2 drop wrote a
register that does not exist" below. **The companion EL1-side check described in
the same section is designed and not yet written**, and is called out again there
rather than left to be assumed from the tense.

`docs/M7_13_HARDWARE_NEUTRALITY.md` lists GICv2 as item 4 on the list of things
"works on any phone" would require, and adds the sentence this document exists to
make true: *"behind the same discovery seam GICv3 already sits behind — the seam
exists, which is why this is a driver and not a redesign."*

The seam did not exist. What existed was a GICv3 driver whose types, register
layout and marker were named `gic_v3` all the way up into `aarch64_boot.cpp`,
and a device-tree walker that would refuse any machine whose controller did not
say `arm,gic-v3`. Adding a second controller behind that shape means adding a
second everything, and a kernel whose whole argument is that it stays small
enough to read completely (`docs/M7_0_KERNEL.md`, `docs/M7_10_LINE_COUNT.md`)
cannot pay for its interrupt code twice.

So this document answers one question first and the GICv2 register programming
second, in that order, because the second is nine registers and the first is the
thing that decides whether the third controller is cheap or expensive.

## The question: what is the smallest interface?

Every use of the interrupt controller in the tree, found by reading rather than
by recalling:

| Site | What it wants |
| --- | --- |
| `cookie_aarch64_boot_main` manifest | the MMIO windows to map as device memory |
| `cookie_aarch64_boot_main` init | one call, given the timer PPI and the device PPI |
| `cookie_aarch64_irq_dispatch` entry | which interrupt is asserting |
| `cookie_aarch64_irq_dispatch` exit | this interrupt is finished |
| `interrupt_attach` / `interrupt_complete` dispatch | this PPI is now masked / unmasked |

That is the whole surface. **Five things, and four of them are one call each.**
The interrupt *policy* — who owns a source, when it is masked, when a driver may
be told it asserted — is `InterruptTable` and `InterruptDeliveryTable`, which are
portable core code and do not know a GIC exists. The controller is only ever
asked to make a decision that has already been taken true in hardware.

So the interface is:

```
initialize_gic_primary_cpu(topology, timer_intid, device_intid) -> GicController
gic_acknowledge(controller)                 -> intid
gic_end_interrupt(controller, intid)
gic_set_ppi_masked(controller, intid, masked)
```

plus a discovery result that carries **an ordered list of MMIO windows and a
version tag**, and nothing else the caller has to interpret.

### Decision: the version is data in a struct, not a vtable and not a call site

Three shapes were available.

1. **Two code paths.** `if (gic_v3) … else …` at each of the five sites above.
   Rejected: it is five branches in `entry`, the category with the least
   business knowing what a redistributor is, and the sixth site added later will
   be the one that forgets.
2. **A function-pointer table.** The conventional answer, and it would work.
   Rejected for two reasons that are specific to this kernel rather than
   general. It is an indirect branch through a global in a kernel that has no
   virtual functions anywhere and asserts that fact in its build
   (`-fvisibility=hidden`, a relocation gate, `no vtables` in
   `core/oskernel/CMakeLists.txt`) — one indirect call target is a small thing
   to give a control-flow attack and this kernel currently gives it none. And it
   costs a static initialised table of pointers, which is exactly the
   `R_AARCH64_RELATIVE` relocation the position-independence work spent effort
   *not* accumulating.
3. **A tag in the state the caller already holds.** `GicController` already had
   to exist and already had to be passed to every operation. It gains one
   `GicVersion` field, and the three per-interrupt operations become a branch on
   it *inside the driver*, where the register layout is already known.

Shape 3 is what landed. The cost is one predictable branch per interrupt; the
benefit is that `aarch64_boot.cpp` contains the string "v3" exactly nowhere, and
that adding a controller is adding a `case` in one file rather than an argument
to a discussion in four.

### Decision: `GicController::per_cpu` is one field for two different frames

GICv3 programs a PPI's enable bit at the **redistributor** and acknowledges at a
**system register**. GICv2 programs the same bit at the **distributor** and
acknowledges at a **memory-mapped CPU interface**. Those are not the same
register in the same place, and the temptation is two structs.

They are, however, the same *role*: both controllers have a global frame shared
by all CPUs and a per-CPU frame, and Cookie needs exactly one of each. So
`GicController` holds `distributor` and `per_cpu`, and which register within
them a given operation uses is the driver's business. This is why
`gic_set_ppi_masked` now takes the whole controller rather than a base address:
the old `gic_v3_set_ppi_masked(redistributor, …)` signature *required* the
caller to know that the enable bit lives at the redistributor, which is a GICv3
fact leaking into `entry`.

### Decision: discovery returns windows, not names

`GicV3Discovery` had a `distributor` plus a `redistributors[8]` array plus a
count, and boot mapped them with one call and one loop. `GicDiscovery` has
`regions[9]` and a count, and boot maps them with one loop. The distributor is
`regions[0]` on both controllers; the rest are redistributors on v3 and the CPU
interface on v2.

The property this buys is worth naming, because it is the one that makes the
next controller cheap: **the code that decides what must be mapped as device
memory no longer needs to know what kind of controller it is mapping.** A
machine layer that needs a third window for a hypothetical controller adds it to
`regions` and nothing above changes.

Two version-specific fields remain in the struct — `redistributor_stride` and
`declared_redistributor_regions` — rather than being hidden behind an accessor.
They are v3 device-tree properties, `valid()` requires them to be absent on a v2
node, and pretending otherwise would be tidier and less true.

### Decision: one walker, not two

`discover_gic` is `discover_gic_v3` with the compatible test widened and the
tuple arity made a function of the version. The alternative — a second walker —
would have duplicated `read_be32`, `read_cells`, `compatible_contains`, the
cells-tolerance fix and the depth handling, and would have re-litigated the
`#size-cells = <0>` lesson that `docs/M7_13_HARDWARE_NEUTRALITY.md` and this
project's memory both record as having had to be learned twice already.

Compatible strings recognised, in order of the test:

| String | Version | Where it appears |
| --- | --- | --- |
| `arm,gic-v3` | v3 | QEMU `virt,gic-version=3`, ARMv8.2+ SoCs |
| `arm,gic-400` | v2 | the overwhelming majority of GICv2 phone SoCs |
| `arm,cortex-a15-gic` | v2 | QEMU `virt,gic-version=2`; older SoCs |
| `arm,cortex-a7-gic`, `arm,cortex-a9-gic` | v2 | older ARM SoCs, listed because they cost one line each |

v3 is tested first. A node claiming both is v3, which is the honest reading of a
`compatible` list whose first entry is the more capable interface.

## GICv2, and where it is not GICv3 minus features

The register programming is short. What is worth writing down is the four places
where GICv2 is a *different* machine rather than a smaller one, because each is
a place a driver written by analogy to the v3 one would be silently wrong.

### The CPU interface is memory, not a system register

GICv3's `ICC_IAR1_EL1` / `ICC_EOIR1_EL1` are system registers, and the boot path
turns them on with `ICC_SRE_EL1`. GICv2 has `GICC_IAR` at offset `0x0C` and
`GICC_EOIR` at `0x10` of a mapped frame, and `ICC_SRE_EL1` **does not exist** on
a machine that only implements GICv2 — reading it is UNDEFINED, not merely
useless.

That has a consequence outside the driver, and it is the emulator-leniency
finding this milestone produced: see below.

### PPI enable lives at the distributor

`GICR_ISENABLER0` is a redistributor register in v3. In v2 the same bits are
`GICD_ISENABLER0` at distributor offset `0x100`, banked per CPU by the hardware.
`gic_set_ppi_masked` branches on this and is the only place that needs to.

### Group assignment is firmware's job, not Cookie's

The v3 path writes `GICR_IGROUPR0` to move its two PPIs into Group 1, because
`ICC_IGRPEN1_EL1` is what it then enables. Cookie does **not** write
`GICD_IGROUPR0` on v2, deliberately.

On a GICv2 with the Security Extensions — which is every real phone SoC, where
Cookie runs in the non-secure world under TF-A — `GICD_IGROUPRn` is RAZ/WI from
non-secure. The write would do nothing and would look like it did something.
Group assignment is performed by secure firmware before it hands off, and a
non-secure kernel that finds its PPIs in Group 0 cannot fix that by writing a
register it does not own; it simply never receives them. On a GICv2 *without*
the Security Extensions — which is QEMU `virt` with `secure=off` — the register
is not implemented at all and every interrupt is in the single group that
`GICD_CTLR.Enable` controls.

So the write is a no-op in both worlds, and omitting it is one fewer line and
one fewer false impression.

### `GICC_PMR` starts masking everything

`ICC_PMR_EL1`'s reset value is architecturally 0 and the v3 path sets it. So
does the v2 path, to `0xF0` rather than `0xFF`, matching what Linux's own GICv2
driver uses: under the Security Extensions the non-secure view of priority is
the secure value shifted, and `0xF0` is the value that is unambiguous in both
views. The PPI priority Cookie assigns (`0x80`) passes that mask in both.

### Decision: the v2 path verifies its CPU interface came up

The v3 path re-reads `ICC_SRE_EL1` after writing it and fails closed if the
write did not take, which is what catches an EL2 that did not permit it. The v2
path has an equivalent and now uses it: after writing `GICC_CTLR`, it reads the
enable bit back and refuses with `cpu_interface_unavailable` if it is clear.

That is not ceremony. A wrong `GICC` base from a malformed device tree, or a
controller whose non-secure half firmware never enabled, both present as writes
that appear to succeed and an interrupt that never arrives — a silent hang at
the first scheduling decision, which is the failure mode this project has
already paid for twice.

## What this requires of real silicon that QEMU does not enforce

Stated explicitly, per `docs/SECURITY_AUDIT_2026_08_16.md` and the failure mode
`docs/M7_13_HARDWARE_NEUTRALITY.md` opens with. Each of these is code that is
correct under QEMU and depends on something a real GICv2 machine may not
provide.

1. **Firmware must have placed the timer and device PPIs in Group 1
   non-secure.** Cookie does not and cannot do this from the non-secure world.
   QEMU `virt` with `secure=off` implements no Security Extensions, so every
   interrupt is in the one group `GICD_CTLR` enables and the question does not
   arise. On a phone it arises on every boot, and the symptom of getting it
   wrong is not an error — it is a machine that boots, prints, and then never
   receives a timer interrupt.

2. **`GICC_EOIR` must be written with the value `GICC_IAR` returned.** For SGIs,
   GICv2's `GICC_IAR` returns the requesting CPU in bits [12:10] and the EOI
   must carry it back. `gic_acknowledge` masks to the 10-bit interrupt ID, which
   is exact for the PPIs and SPIs Cookie handles and would be wrong for an SGI.
   Cookie is single-CPU and generates no SGIs, so there is no live defect; the
   day SMP lands, this mask is one of the things that has to change, and it is
   written here rather than discovered then.

3. **`EOImode` must be clear.** Cookie writes the whole of `GICC_CTLR` rather
   than setting a bit into whatever firmware left, so `GICC_CTLR.EOImodeNS` ends
   up 0 and a write to `GICC_EOIR` both drops priority and deactivates. If it
   were left set — as some hypervisor handoffs do — every interrupt would remain
   active forever after its first delivery and the machine would take exactly
   one timer tick. Writing the register whole is what makes this true; a
   read-modify-write would not.

4. **The distributor and CPU-interface windows must be device memory, and the
   device tree must say where they are.** Cookie maps `regions[0..n]` as
   `MachineMemoryKind::device` from the DT and hard-codes no address. This is
   already right and is listed because it is the one that would be easy to
   regress.

5. **`ICC_SRE_EL2` must not be written on a machine without the GICv3 system
   register interface.** This is the finding, and it is below.

### Finding: the EL2 drop wrote a register that does not exist on a GICv2 SoC

`aarch64_start.S` writes `ICC_SRE_EL2` unconditionally during the EL2→EL1 drop,
with a comment saying so: *"This instruction only exists on a machine that
implements the GICv3 system registers, which Cookie already requires."* The
premise stopped being true in this milestone.

On a GICv2-only SoC — `ID_AA64PFR0_EL1.GIC == 0` — that `msr` is UNDEFINED. It
is executed at EL2, before `VBAR_EL2` has been given a handler, so the machine
takes an exception to a vector that does not exist and dies before the first
instruction that could report anything. That is precisely the class this
project's audit note describes: correct under the emulator, fatal on the
silicon.

No gate would have found it either, and that is the more uncomfortable half.
QEMU `virt,gic-version=2` is only ever entered at **EL1** in the runs below
(`virtualization=off`), so the drop path does not execute; and every run that
does exercise the drop uses a GICv3. The two conditions that have to coincide —
GICv2 *and* an EL2 handoff — are exactly the phone case, and are the one
combination no configuration in CI produces.

**Fixed here**, by reading `ID_AA64PFR0_EL1.GIC` and skipping the write when the
field is zero. Four instructions, and they land with this document because a
machine that dies before its first output is not something to leave standing
while the rest of the milestone is built.

**Not yet written, and deliberately named rather than implied:** the same check
belongs before the v3 path touches `ICC_SRE_EL1` at EL1, so that a device tree
claiming `arm,gic-v3` on a CPU with no system-register interface is refused with
a reason instead of taking an undefined instruction inside GIC initialisation.
That refusal needs an error code and a check in `aarch64_gic_v3.cpp`; it is part
of the GICv2 milestone proper. The EL2 case is separated from it because only
the EL2 case kills the machine before anything can report why.

## How it is gated

`kernel-arm64-native` gains a fourth boot run:
`qemu-system-aarch64 -machine virt,gic-version=2,virtualization=off -cpu
cortex-a72`, asserting **the same marker manifest** as every other run.

Not a reduced list. `.github/scripts/cookie-boot-markers.txt` exists because
that list was written three times with three different subsets and the EL2 path
was verified against 13 of the 33 proofs the EL1 path was; the header of that
file is the argument and it is not being made again. Every proof in the tree —
two EL0 processes, native IPC, timer preemption, the driver interrupt chain,
address-space create/destroy, the pager, the compiled program — runs on GICv2 or
the gate is red.

One marker changed to make that possible, and the change is the point rather
than an accommodation. `COOKIE:M7.5g:GICV3` became `COOKIE:M7.5g:GIC`: the
manifest's claim is *an interrupt controller initialised*, which is true on both
machines, and asserting `GICV3` on a GICv2 boot would have been a lie or a
shorter list. Which controller it actually was is now
`COOKIE:M7.18:GICV2` / `COOKIE:M7.18:GICV3`, checked by a `--gic v2` / `--gic
v3` argument to `check-boot-markers.sh` — the same treatment PAN already gets,
for the same reason its own comment gives: *"A difference that real belongs in
an argument, not in a silently shorter list."*

`tools/boot-cookie-qemu.sh` gains a `gicv2` mode so the run is reproducible on a
developer machine without a CI round trip.

## What this does not decide

- **Whether a third controller is worth carrying.** The seam is now real and the
  marginal cost of a controller is a `case` and a register block, but that is an
  argument for it being *cheap*, not for it being *wanted*.
  `docs/M7_13_HARDWARE_NEUTRALITY.md`'s open question — whether Cookie should
  simply require GICv3 and say so — is answered "no" only for v2, and only
  because v2 is the majority of the phones Cookie is aimed at.
- **SPIs.** Cookie routes two PPIs. Every real driver will want a shared
  peripheral interrupt, which on both controllers means distributor
  configuration Cookie does not yet do (`GICD_ITARGETSR` on v2, `GICD_IROUTER`
  on v3) and a source-number space wider than 32. That is the user-space driver
  framework's problem and it will press on this interface first.
- **SGIs and therefore SMP.** Named above as requirement 2. Nothing here is SMP
  work and the single-CPU assumptions in `initialize_gic_primary_cpu` are
  unchanged — it still finds *this* CPU's frame and configures nothing else.
- **GICv2m, GICv3 ITS, MSIs.** No Cookie driver has a PCIe device yet.
- **Whether firmware left the controller in a usable state.** Cookie now
  *detects* one form of that (the CPU interface readback) and refuses. It does
  not attempt recovery, and should not: a non-secure kernel that finds the
  controller misconfigured is looking at a firmware bug, and booting anyway with
  interrupts it cannot receive is worse than refusing with a name.
