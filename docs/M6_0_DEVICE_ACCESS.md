# M6.0 - Device Access Model

A hardware-neutral OS cannot avoid the question this milestone answers: how does
code that must touch hardware get the authority to do so, without that authority
becoming a way around every boundary above it?

ENML has spent five milestones building boundaries - typed identities, brokered
service access, sandboxed services, sealed key material, a compositor that
refuses capture by default. A driver holding unrestricted authority makes all of
them advisory. So the device layer has to be designed as a security boundary
from the start, not retrofitted once drivers exist.

## What the references establish

**Drivers are where the bugs are.** Driver code has been measured at two to
seven times the defect density of the rest of the kernel, and the large majority
of crashes on at least one commodity desktop OS were attributed to drivers. The
reasons given are structural rather than incidental: kernel code is harder to
write, the good analysis and debugging tools target user space, and driver
authors are often less specialised than core kernel developers. None of that
changes by being careful.

**The useful split is by performance and priority.** Across ~300 analysed
drivers in three families, fewer than 34% of functions - about 37% of the code -
were on the data path or ran at high priority. Everything else was
initialization, configuration, shutdown, diagnostics and error handling. In one
network driver, 82% of the revisions landed in that non-critical portion. So the
majority of driver code, and the large majority of the churn, can leave the
kernel; and when it does, measured throughput and CPU cost are indistinguishable
from the monolithic driver, because the transitions happen at startup and not on
the data path.

**The obvious split is the wrong one.** Existing device-dependent versus
device-independent factorings are not usable as a protection boundary: those
halves communicate constantly and move large structures. The seam has to be
chosen so that it is crossed rarely and carries little.

**Neutral hardware vocabularies already exist.** The mainline display stack
decomposes any display pipeline - graphics card or SoC unit - into framebuffer,
plane, CRTC, encoder, bridge, connector/panel, with drivers advertising what they
support (formats, modifiers, valid attachments) and userspace choosing from what
was advertised. Configuration is an *atomic commit*: a batch of property changes
that is validated in full before any of it is applied. The predecessor interface
is deprecated for exactly the reasons ENML cares about - no zero-copy, and too
little expressiveness to describe real pipelines.

## Where ENML diverges

The microdriver design gives its user-mode half access to the I/O port space and
maps the device's registers into it. For its stated goal - fault isolation,
keeping a driver bug from panicking the kernel - that is a reasonable trade: a
crash in that component stays a crash.

It is the wrong trade for ENML, because ENML's boundary is a security boundary
and not only a fault boundary. A component that can reach arbitrary I/O ports is
not confined by anything; it is a kernel with extra steps. The same paper is
candid that its own recovery story is incomplete - a bad deallocation or a
corrupted shared structure from the user side can still take down the kernel
half - which is precisely the difference between "this component may fail" and
"this component may not be trusted".

So ENML takes the split and refuses the grant:

- **Port I/O authority is not representable.** There is no field for it in
  `DeviceAccessPolicyV1`, and adding one would defeat the format. A bounded MMIO
  window can be checked; "the I/O port space" cannot.

- **MMIO is an explicit allow-list of bounded windows.** Never a mapping of "the
  device's registers" whose extent nobody wrote down. Base and length are both
  explicit, empty windows are rejected, and a window whose end wraps past the
  top of the address space is rejected - it describes nothing, and every
  containment check would be meaningless for it.

- **Grants are canonical**: strictly ascending and non-overlapping. One
  authority then has exactly one encoding, and an overlap cannot present the
  same registers twice under two different access modes, which would make the
  effective permission depend on which grant a checker happened to match first.

- **DMA is treated as what it is.** A device that masters the bus with no IOMMU
  in front of it can read and write all of physical memory - including the
  memory of everything it was supposed to be isolated from. Moving its driver
  out of the kernel isolates that driver from the kernel's control flow, not
  from its memory. The parser therefore refuses a record that claims an
  out-of-kernel component is isolated while its device has unconfined DMA.

The marshaling problem is also worth naming. The microdriver tooling depends on
hand-written pointer annotations, and its authors note that a missing or
incorrect annotation produces an incorrectly marshaled structure - a
memory-safety bug generated by the boundary that was supposed to contain one.
ENML does not have this problem and must not acquire it: OSIDL already generates
typed, bounded, explicitly little-endian wire formats with no native layout
serialization. Anything crossing the device boundary goes through OSIDL. There
will be no annotation dialect.

## What the format is

`DeviceAccessPolicyV1` (`EDA1`), 32-byte header plus up to four 24-byte grant
records. Same construction rules as every other record in the tree: explicit
little-endian, fixed capacity, declared length must equal real length, reserved
fields rejected when nonzero, unknown discriminants rejected rather than
defaulted.

| Field | Meaning |
| --- | --- |
| `ExecutionDomain` | `kernel_resident` (data path / high priority) or `isolated_user` |
| `DmaCapability` | `none`, `iommu_confined`, or `unconfined` |
| `MmioGrant[]` | base, length, and `read_only` / `read_write` |

**The default policy grants nothing** - isolated, no DMA, no windows. This is
the counterpart of the boot state defaulting to unverified: a caller who forgets
to parse, or parses and ignores the error, holds a policy that confers no
authority. Authority is something a record has to actively confer.

`confined()` is the accessor callers are meant to ask. Testing the execution
domain says only where code runs; it says nothing about what the code can reach.

### Honest degradation, again

A kernel-resident driver for a bus-mastering device on an IOMMU-less platform is
a legitimate, honest configuration, and it parses. It simply does not report
itself as confined. This is the same discipline as the M5.5 platform capability
set: represent the assurance the platform actually provides, rather than the
assurance we would like it to have. A format that cannot express the degraded
case forces a lie, and a lie in a policy record is worse than a known weakness.

## What this milestone does not yet do

- **No driver runs under this policy yet.** The format and its enforcement point
  exist and are gated; the first driver to be split against them does not.
- **No IOMMU programming.** `iommu_confined` currently records a platform fact.
  Establishing and maintaining the mapping is separate work.
- **No interrupt authority.** Interrupt routing is deliberately absent rather
  than half-specified; it needs its own threat model.
- **No atomic commit for device configuration.** The display stack's
  check-then-commit discipline - validate the whole batch, then apply all of it
  or none - is the right shape for ENML's compositor and for device
  reconfiguration generally, and is tracked separately.

## Gates

`m6-device` runs under GCC, Clang, sanitizers, and natively on AArch64. The
AArch64 job is not ceremony here: this parser does 64-bit address arithmetic at
the edge of the address space, and that arithmetic must hold on the primary
target rather than only on the build host.

`device_access_fuzz` runs in the per-PR smoke set and in the nightly, asserting
on every accepted record that grants are bounded, canonical and non-overlapping,
that what was granted is permitted and the byte on either side is not, that a
read-only window never satisfies a write, that `confined()` agrees with the
fields it derives from, and that re-encoding reproduces the input exactly - so
the stored form and the enforced form cannot drift apart.
