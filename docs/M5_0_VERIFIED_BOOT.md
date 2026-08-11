# M5.0 — verified boot foundation

## Implementation authority

**References teach principles. ENML determines implementation. External systems are not the design specification.**

The supplied secure-boot material informs this milestone: the embedded-Linux
chain-of-trust model (ROM → bootloader → kernel → rootfs, each stage verifying
the next before transferring control), the hardware root-of-trust model (an
immutable ROM authenticating against an on-chip key hash in one-time-programmable
fuses, with no runtime path to interrupt or bypass it), the observation that
signing *configurations* rather than individual images is what prevents
mix-and-match attacks, and Merkle-tree block integrity checked lazily on access
for a read-only root filesystem.

None of those define ENML's boot record format, key hierarchy, service topology
or lifecycle vocabulary. Where this document names such a mechanism it is naming
the *class* of mechanism a target platform must provide.

ENML adopts no vendor's container layout, MOK list, or SoC fuse map as its
design. **UEFI is deliberately left open rather than refused.** On ARM64 it is
the de-facto firmware interface — advocated by ARM's SystemReady programme,
implemented by EDK2, LK and U-Boot, and the boundary at which block IO, RNG and
hash services are already standardised. Refusing it means reimplementing those
against every SoC, which enlarges the trusted surface this project exists to
keep small. Whether ENML boots via a UEFI application or a bespoke loader is a
platform decision to be made with the target, and the reasoning recorded — not
a principle to settle in advance.

## Why this is the next milestone

M0–M4 built a userspace whose security properties are real but conditional. Every
guarantee — capability-scoped storage, server-held rights, deterministic
revocation, sandboxed applications, a trusted shell that cannot be impersonated
— assumes the code enforcing it is the code we shipped. Nothing currently
establishes that.

Stated plainly: **an attacker who can modify the boot chain inherits every
authority ENML has carefully refused to hand out.** The existing work is not
wasted by that gap, but it is bounded by it, and the boundary should be closed
before more authority is layered on top.

M2.7 already anticipated this. `MonotonicSecurityState` exists as an interface
with an explicit note that anti-rollback may not be claimed "until KRG
publication is integrated with a real hardware/verified-boot monotonic source
using a reviewed crash-consistent protocol." M5.0 is where that source arrives.

## Hardware neutrality is the requirement, not a constraint on it

An earlier draft of this document said choosing a target SoC was a prerequisite
for M5.0. That was wrong, and wrong in a way worth recording: it treated
hardware neutrality as something to be traded away for security, when the
product goal is that the two hold together.

The portable-TEE work in the supplied references shows the shape that actually
works. One implementation spans a dozen SoCs plus an emulator by putting a
vendor-neutral API on top, keeping a small explicitly-scoped platform port at
the bottom, and letting everything between them be portable. Neutrality there is
not achieved by avoiding hardware; it is achieved by naming exactly what the
hardware must provide and isolating the part that knows which hardware it is.

ENML takes the same shape:

- **A platform contract, not a platform choice.** ENML states what it needs — an
  immutable first stage, measurement, sealed storage, a monotonic counter, an
  entropy source — and adapts to what a platform actually offers.
- **A narrow adaptation boundary.** Platform-specific code is confined to the
  port. Nothing above it names a vendor, a fuse map, or a container format.
- **Standard interfaces where they exist.** Where an industry-neutral interface
  already exists for a capability, adopting it is cheaper and smaller than
  reimplementing per SoC — and a smaller trusted surface is the second product
  priority, not a side benefit.
- **An emulated reference platform.** The whole stack must build and be gated
  against an emulator, so verified boot is developed and regression-tested
  without a board. Physical SoCs then become ports rather than prerequisites.

### Honest degradation is what neutrality costs

Running on unknown hardware means the security level is not uniform, and an OS
that cannot tell a hardware-rooted device from a bare one will make identical
promises about both. Neutrality therefore obliges ENML to **measure and report
what it actually got**, not to assume the maximum.

`BootStateV1` carries a platform capability set for exactly this: immutable
first stage, root of trust for measurement, for storage, for reporting, and a
monotonic counter. Absent capabilities are facts, not failures — a platform may
legitimately provide none, and policy above may legitimately choose to run
degraded. What it may not do is inherit an assumption.

Two claims are refused outright at the parser, because they are claims the
platform cannot back:

- a **closed, verified** device that does not declare an immutable first stage;
- a **nonzero security version** without a monotonic counter, since rollback
  resistance is a claim about that counter and nothing else.

This is what keeps "hardware neutral" from quietly meaning "secure only on the
hardware we happened to test".

## Chain of trust

ENML adopts the staged-verification principle, with four links:

| Link | Verifies | Rooted in |
| --- | --- | --- |
| Immutable first stage | The bootloader | On-chip key hash in one-time-programmable storage |
| Bootloader | Kernel, device tree and boot configuration together | Key embedded in the verified bootloader |
| Kernel | The root filesystem | Signed root hash of a block-level Merkle tree |
| ENML | Application code | Existing M1 signer-bound package identity |

The fourth link already exists. M1's generation-bound, signer-bound package
identity with immutable content digests is the top of this chain, and M5.0
connects the three links beneath it rather than inventing a parallel scheme.

### Invariants

- Each stage verifies the next **before** transferring control. Verification
  failure halts or enters a defined recovery state; it never degrades to
  unverified execution, and there is no "warn and continue" mode.
- The root of trust is immutable and in hardware. A root that software can
  rewrite is not a root.
- There is no runtime path — debug, recovery, factory, developer or diagnostic —
  that bypasses verification on a device in the closed lifecycle state. If such a
  path exists for development, entering the closed state must destroy it.
- **The unit of signing is the configuration, not the image.** A signature over
  a kernel alone permits pairing that kernel with a different device tree or
  boot argument set. The signed object must bind every component of a boot
  together, because the attack is composition, not substitution.
- Boot arguments are inside the signed configuration. A boot argument set that
  can be edited is a kernel command line an attacker controls.

### The patched-configuration problem

The invariant above is necessary but, stated naively, unimplementable. Real
platforms *patch* the device tree, bootconfig and kernel command line during
boot — memory regions, watchdog nodes, RNG seeds, board revisions. A
configuration that is signed at build time and then patched at boot cannot be
verified against its signature afterwards, so "measure the final configuration"
and "sign the configuration" are in direct tension.

Three resolutions exist, and ENML must choose one explicitly rather than
discover the conflict during bring-up:

1. **Do not measure the final configuration.** Cheapest, and it silently drops
   exactly the surface the invariant exists to protect. Rejected.
2. **Diff the final configuration against the signed one and enforce an
   allow-list of patchable nodes.** Anything outside the allow-list fails the
   boot. Preferred: it keeps the signature meaningful while admitting the
   patching real hardware requires, and the allow-list is itself reviewable.
3. **Acquire every patched value during the measurement phase and emit a fixed
   configuration.** Strongest, and it demands the platform surface all its
   dynamic values up front, which not every SoC does.

ENML takes option 2 as the default and option 3 where the platform allows it.
The allow-list is part of the signed configuration, not a runtime setting —
otherwise it is just an attacker-editable exemption list.

### Static versus dynamic root of trust

The chain above is a *static* root of trust for measurement: trust begins at the
immutable first stage and extends forward. Its weakness is that boot firmware is
large, vendor-supplied and historically vulnerable, and every line of it is
inside the TCB.

A *dynamic* root of trust re-establishes measurement after boot firmware
completes, in a deliberately reduced state — other cores parked, DMA disabled —
so a compromise in early firmware does not automatically inherit the chain. It
narrows the TCB at the cost of platform-specific launch machinery.

ENML does not choose between these in the abstract. The requirement is that the
target platform's capability be assessed and recorded: if it supports a dynamic
measurement launch, M5.0 must state whether ENML uses it and why; if not, M5.0
must state that the boot firmware is inside the TCB and is therefore a
supply-chain dependency, not merely a bootstrap step.

### Update safety

Verified boot and safe update are the same problem viewed from two directions.
A design that verifies strictly and updates carelessly bricks devices; one that
updates freely re-admits rollback.

- Boot slots are paired, with the active slot selected before verification and
  a failed boot falling back to the previously good slot. An update that cannot
  fail back is an update that turns a bad signature into a dead device.
- Rollback protection is a distinct verification step with its own state, not a
  side effect of signature checking. A correctly signed *old* image is exactly
  the attack rollback protection exists to stop.
- Root filesystem verification failure has an explicit, chosen policy —
  restart, read error, or halt. Leaving it to a default means the behaviour
  under attack is whatever the block layer happened to do.
- Root filesystem integrity is verified per block on access against a Merkle
  tree whose root hash is itself signed. Whole-image verification at boot is
  rejected: it costs boot time proportional to image size and leaves the image
  unprotected after mount.
- Key rotation is signed by the keys currently in force. A rotation the current
  chain cannot authenticate is an attacker's rotation.
- Revocation is explicit and monotonic. A retired signing key must not become
  valid again by rolling state backwards.

## Boot state as an ENML object

The boot chain's output is not merely "we booted." It is evidence, and ENML
treats it the way it treats every other trusted fact: as a bounded, explicitly
serialized record produced by trusted state and never claimed by a caller.

`BootStateV1` records the verified security version, the lifecycle state of the
device, and the measured digest of each verified link. It follows the existing
substrate rules — explicit little-endian, no native C++ layout serialization,
fixed capacity, fail closed on any malformed field.

Rules:

- The record is produced by trusted early boot and is **read-only** to
  everything above it. No service, application or control channel may write,
  extend or re-attest it.
- It is never a public application ABI. Applications do not learn digests,
  fuse values, key identities or lifecycle state; those are exactly the values
  an attacker wants for fingerprinting and targeting.
- A device that did not complete a verified boot has a `BootStateV1` that says
  so explicitly. Absence of the record is treated as failure, never as success.

## Binding the key hierarchy to the boot state

This is the ENML-specific design decision and the reason the milestone is worth
doing now rather than later.

M2.7 defines a downward-only protection hierarchy: system → profile →
application. Its system root is currently bound to a `KeyProtectionBinding`
held by the provider. M5.0 extends that binding to include the verified boot
state, so that:

- system-scope key material is unsealable **only** under the boot state that
  created it;
- a device booted through a different chain — an attacker's kernel, an
  unsigned rootfs, a rolled-back bootloader — derives a different binding and
  therefore cannot unseal system keys, without needing to detect the attack;
- rollback is resisted by construction rather than by a check that could be
  skipped, because the security version is an input to the binding.

This is what makes `MonotonicSecurityState` real: the monotonic source becomes
the platform's counter, and KRG publication binds to it under a crash-consistent
protocol.

This construction is not novel and should not pretend to be. Layered derivation
in which each boot stage contributes its measurement to the secret handed
forward is the established pattern — the industry calls it a DICE chain, and
Android carries it through to protected-VM firmware. ENML's contribution is not
the construction but where it *terminates*: in the existing M2.7 protection
hierarchy and its `PrincipalId`-scoped roots, rather than in a separate
attestation silo bolted alongside. Naming the prior art matters here, because a
hand-rolled variant of a well-analysed key-derivation chain is precisely the
kind of thing that looks fine and is not.

The consequence is deliberate and must be designed for, not discovered: **a
legitimate update changes the boot state, so system-scope material must be
re-derivable across an authorized version increase and not across an
unauthorized one.** An update protocol that cannot do this bricks the device on
every update; one that is too permissive re-admits rollback.

## Exit criteria

M5.0 may not leave draft until:

- a target platform is chosen and its root of trust, fuse programming model,
  lifecycle transition and monotonic counter are documented;
- a device in the closed lifecycle state refuses an unsigned bootloader, an
  unsigned or mismatched kernel/device-tree/boot-argument configuration, and a
  root filesystem whose signed root hash does not verify;
- no bypass path survives the closed transition, demonstrated by attempting each
  known development path on a closed device;
- rootfs block corruption is detected on access rather than at mount;
- `BootStateV1` parses under fuzzing with the same bounded, fail-closed
  discipline as `WireHeaderV1` and `KRG1`, and has a fuzz target in the nightly
  matrix;
- system-scope key material fails to unseal under a modified boot state, proven
  by a negative test rather than by argument;
- an authorized version increase re-derives system-scope material and an
  unauthorized decrease does not;
- boot-to-shell time is measured and carries a budget, extending the M4.2 gate.

Security-negative tests must cover at minimum: unsigned bootloader, unsigned
kernel, valid kernel paired with a foreign device tree, edited boot arguments,
tampered rootfs block, rolled-back security version, and unseal attempted under
a foreign boot state.

## Explicit non-goals

M5.0 is verified boot only. It does not deliver full-disk encryption keyed to
user authentication, a TEE or secure-world runtime, baseband isolation, attested
remote provisioning, an encrypted recovery path, or a production key-management
and signing infrastructure. Each is separate work with its own threat model.

It also does not make the device forensically resistant. Verified boot
establishes that the running software is the software we shipped. It says
nothing on its own about data at rest under physical attack.
