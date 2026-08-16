# M7.13 — What Cookie assumes about a machine

Cookie is meant to run on whatever phone it is put on. Today it runs on one
machine: `qemu-system-aarch64 -machine virt,gic-version=3 -cpu cortex-a72`.
This document is the measured difference between those two sentences.

Everything below was read out of the tree rather than recalled, and each item
says where. It is deliberately not a plan — a plan written before the inventory
would be a guess about which gaps matter.

## The failure mode this document exists for

**An emulator is a lenient machine.** Two of the findings below are not
"unfinished work"; they are code that is *wrong on silicon and correct under
QEMU*, and the boot proof passes on both. That asymmetry is the thing to be
afraid of, because every gate Cookie has runs on the lenient machine.

Both have the same shape: hardware offers a fast path that requires the kernel
to say something explicit, the kernel never says it, and the emulator does the
slow safe thing anyway.

### Finding 1: every translation was global (fixed here)

`install_process_translation` switches `TTBR0_EL1` **without flushing the TLB**,
and says so:

> Unique, non-retired ASIDs let Cookie switch roots without flushing the entire
> EL1 TLB.

That is the right design and the reason ASIDs exist. It requires that entries be
*tagged* with an ASID, which requires the `nG` bit in each leaf descriptor.
**No descriptor in the tree set `nG`.** Bit 11 appeared nowhere in
`core/oskernel/`.

So every entry Cookie installed was global: it matches whatever ASID is in
`TTBR0_EL1`. And every Cookie process maps its code at `user_code_virtual` and
its stack at `user_stack_virtual` — *the same virtual addresses*. On real
hardware, a process switch would leave the outgoing process's entries live and
matching at exactly the addresses the incoming one uses. That is not a
performance defect. It is the isolation the address-space model exists to
provide, absent.

It also silently emptied `retire_process_asid`. `tlbi aside1is` invalidates
entries *tagged with an ASID*; global entries carry no tag, so a destroyed
space's translations would have survived the invalidation that
`docs/M7_11_MEMORY.md`'s two-phase destroy is built around.

QEMU hides it: TCG's software TLB is flushed when `TTBR0_EL1` is written, so the
fast path Cookie was relying on never actually got taken.

Fixed by setting `descriptor::not_global` on every leaf, user and kernel alike —
kernel leaves too, because under TTBR0-only translation the kernel's mappings
live inside each process root and a global kernel entry is one no ASID
retirement can reach. Asserted per permission and per memory kind in
`aarch64_translation_test.cpp`, and on the leaf the builder actually installs in
`aarch64_page_tables_test.cpp`.

**The general lesson, which is the same one `forbidden_by_reservation` taught:
a mechanism that is present is not a mechanism that is engaged.** The ASID
machinery was complete, tested, and documented, and it was tagging nothing.

### Finding 2: instructions were written through a data mapping and never made visible to the instruction fetch (fixed here)

`install_process_a_program`, `..._b_program` and `..._c_program` write A64
instruction words into a page through a normal writable mapping, and then that
page is mapped read-execute and entered.

Between those two things, ARMv8 requires the writer to clean the data cache to
the point of unification and invalidate the instruction cache for the range —
`dc cvau` / `dsb ish` / `ic ivau` / `dsb ish` / `isb`. **There is no cache
maintenance instruction anywhere in `core/oskernel/`** (`dc`, `ic`, `dcache`,
`icache`: zero hits).

On a core with separate, non-coherent instruction and data caches — which is
every phone — the instruction fetch may see whatever was in that physical line
before, and the process executes stale memory. QEMU TCG has no separate
instruction cache and invalidates its translation blocks on writes, so it
executes exactly what was written.

This is the loader's problem in the end — placing a program is what M7.12's
loader will do — but it is *already* the boot proof's problem, and a loader
written on top of a kernel with no cache-maintenance primitive will inherit the
bug rather than introduce it.

Fixed by `aarch64_publish_instructions`, a machine-layer primitive rather than
an inline asm block in the boot file — because M7.12's loader needs exactly this
call, and a loader built on a kernel without one inherits the bug rather than
introducing it.

Two details in it are the interesting part. **Line sizes come from `CTR_EL0`,
not from 64.** Sixty-four bytes is the common answer and not the architectural
one, and a stride *larger* than the implementation's line silently skips lines —
the same defect as omitting the maintenance, but harder to see because it looks
like it ran. **`CTR_EL0.IDC` and `DIC` are honoured**, so on a core whose caches
are already coherent for this purpose the function collapses to its barriers
instead of paying for maintenance nobody needs on the path that places every
program.

## What Cookie requires of a machine today

Measured. Each is a real constraint, not a preference.

| Requirement | Where | Phones this excludes |
| --- | --- | --- |
| ~~Entry at **EL1**~~ | *Fixed.* `aarch64_start.S` now accepts EL2 and drops to EL1, gated by a second QEMU run with `virtualization=on`. EL3 is still refused, deliberately: secure firmware owns that level. | — |
| **GICv3** | `initialize_gic_v3_primary_cpu`, `gic_v3_discovery` | Every GICv2 SoC. |
| **PL011** UART | `find_pl011` | Almost all of them. Qualcomm (GENI), Exynos, MediaTek all use their own. No console means no bring-up. |
| **Flattened device tree** | `FdtView`, `hardware_inventory` | None — phones are DT, not ACPI. This one is already right. |
| **4 KiB granule, 39-bit VA** | `stage1_va_bits = 39`, `architectural_page_size` | Nothing in practice, but it is *assumed* rather than checked: `TCR_EL1.TG0` is left at its `0b00` default and `ID_AA64MMFR0_EL1.TGran4` is never read. |
| **Loaded at exactly its link address** | `-no-pie`, absolute `SECTIONS` in `aarch64_qemu.ld.in` | All of them. The header now *tells the truth* about which address that is, and a gate checks it — but telling the truth about a single address is not the same as being loadable anywhere. See below. |
| **One CPU** | one runqueue, `initialize_gic_v3_primary_cpu`, no locks anywhere | None, but every phone would run on one core. |

Already portable, and worth saying because the inventory is otherwise a list of
gaps: the physical address size is read from `ID_AA64MMFR0_EL1.PARange` and
clamped rather than assumed (`cookie_ips`), all device addresses come from the
device tree with no hardcoded constants, the timer is discovered including which
PPI is which, and the load address is a machine-port cache variable rather than
a literal in the source.

### The Image header did not describe the image (fixed here)

`aarch64_start.S` publishes an ARM64 Image header declaring
`text_offset = 0x80000`, with a comment saying that matches a linked placement
of `0x4008_0000`. The image is linked at `EMNL_AARCH64_BOOT_LOAD_ADDRESS`, which
is **`0x40200000`**.

So the header tells a bootloader to place the image 512 KiB above the base of
RAM, and the image only runs if it is placed 2 MiB above it. A loader that
honours the header lands Cookie at an address none of its absolute references
match, and it dies before the first instruction that could report anything.

Nothing catches this because the boot proof loads the ELF via
`-device loader,file=…`, which uses the program headers and never reads the
Image header at all. The one field that would be exercised on a phone is the one
field no gate exercises.

Two things followed, and they were separable. The first is done:

**`text_offset` is now derived by the linker script** from the port's load
address and a declared `EMNL_AARCH64_BOOT_RAM_BASE`, the way `image_size`
already was, so the header cannot disagree with the link. The linker asserts
that the RAM base is 2 MiB aligned and that the image is not linked below it;
both are build failures rather than boot failures. The flags word now declares a
4 KiB granule instead of "unspecified", which was a licence to place the image
for a granule Cookie does not use.

And the gap that let it survive is closed: **a CI step reads the header out of
the raw `.bin`** — what a loader is actually handed — and checks `text_offset`
against `__cookie_image_start - __cookie_ram_base` taken from the ELF's symbols,
plus the magic, the size and the flags. The field a phone would use is now the
field a gate exercises.

The second is not:

- **Being loadable only at one address is not phone-neutrality.** A phone's
  bootloader chooses where the image goes. Fixing this properly means
  position-independent early boot — build PIE and apply `R_AARCH64_RELATIVE`
  relocations before anything absolute is touched, which is what Linux's arm64
  `head.S` does and why it does it. That is a milestone, not a patch.

## What "works on any phone" would actually require

In the order the dependencies fall, not the order of difficulty:

1. ~~**Accept entry at EL2 and drop to EL1.**~~ Done. The one thing worth
   carrying forward from it: `ICC_SRE_EL2` has to be set before the `eret`, or
   EL1's own `ICC_SRE_EL1` write traps to EL2 and the GIC initialisation fails
   closed with no interrupt controller. That is the kind of dependency a drop
   sequence copied from a reference would carry silently.
2. ~~**Cache maintenance primitives.**~~ Finding 2, done — `aarch64_publish_instructions`.
3. **A relocatable image.** Until then, "any phone" means "any phone whose RAM
   happens to start where Cookie was linked".
4. **A second interrupt controller.** GICv2, behind the same discovery seam
   GICv3 already sits behind — the seam exists, which is why this is a driver
   and not a redesign.
5. **A second UART.** Same argument. The console is only needed for bring-up,
   which is exactly when it is needed most.
6. **SMP.** Every phone is multi-core, and this is the item that touches the
   most existing code: the scheduler, the rendezvous, every fixed table, and
   every "Cookie is single-CPU today" comment that currently justifies the
   absence of a lock. `docs/M7_11_MEMORY.md` already records one such comment
   in `aarch64_release_address_space`.
7. **PSCI.** Needed for 6, and for power management at all.

None of these is research. All of them are work, and the honest summary is that
Cookie today is a kernel that runs on one emulated board, with a device-discovery
layer that was built as though it would run on more.

## What this does not decide

- **Whether to support GICv2 at all.** It may be reasonable to require GICv3 and
  say so, rather than carry two interrupt controllers in a kernel whose whole
  argument is its size. That is a product decision and belongs with the hardware
  target list, not here.
- **The SMP model.** Big-little scheduling, per-CPU runqueues and the locking
  discipline are a milestone of their own, and choosing them from this document
  would be deciding the largest thing on the list in the smallest amount of
  detail.
- **Which phone first.** Every item above is cheaper against a specific device
  than against "any phone", and the list stays the same either way.
