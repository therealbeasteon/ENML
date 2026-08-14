# M7.2 — Nothing can move off Linux yet, and why

**Status: finding.** No code. This document exists because the de-Linux plan
has an order, and the order silently assumes something that is not true.

## The instruction and the honest answer

The instruction is to move Cookie off Linux entirely. `docs/M7_2_DELINUX.md`
maps that: nineteen coupled files (eighteen today), a ratchet gate that will not
let a twentieth join, and an order — IPC, then process and supervision, then
display buffers, then sandboxing last.

The order is right. It cannot start.

**Not one of the eighteen files can move, because there is nowhere to move them
to. Cookie has no userland.**

## What "no userland" means, measured

Not "immature". Absent.

- **No syscall stubs.** Nothing outside `core/oskernel` references
  `KernelCall`, and nothing anywhere emits an `svc` except the kernel's own
  sources. There is no library a program could link to call `send`, `receive`
  or `reply`.
- **No program format and no loader.** Nothing loads an executable into an
  address space. `aarch64_el0.S` is not a program — it is the kernel's
  *entry into* EL0, the `eret` path.
- **The EL0 "programs" in the boot proof are hand-assembled words.**
  `aarch64_boot.cpp` writes `0xD4000001` — `svc #0` — into pages as raw
  `std::uint32_t` values, along with a branch-to-self. That is the whole of
  what has ever run at EL0 on Cookie.
- **No address space can be created after boot.** `machine_release_address_space`
  returns `machine_errors::unsupported`, and there is no create counterpart.
  Every address space that exists was planned before any process existed
  (`plan_early_boot_memory`). This is M7.11's subject and M7.11 is in progress.

A service is a compiled program that links `osipc`, calls into a kernel, is
loaded into an address space, and is scheduled. Cookie can do the last of those
four.

## Why this was not obvious

Because each part of the plan is individually sound and the gap is between them.

`docs/M7_2_DELINUX.md` reasons about *what each dependency becomes* — and it is
correct on every count. `SOCK_SEQPACKET` really is replaced by the rendezvous;
`SCM_RIGHTS` really is replaced by capability transfer; `SCM_CREDENTIALS` really
is replaced by kernel-attested identity, and `docs/M7_2_SERVER_LOOP.md` has since
shown the multiplexing is deleted rather than ported. Every one of those
statements is about a *destination that does not exist yet*.

The coupling gate reinforces the illusion, because it counts what is coupled and
has no way to notice that the thing being migrated *to* is missing. Eighteen of
eighteen is a true number that says nothing about feasibility. It would read the
same on the day before a service ports and on a day when porting is impossible.

`core/osipc/include/os/ipc/channel.hpp` is the sharpest illustration: it has no
Linux headers at all. It is already substrate-neutral — `os::core::NativeHandle`,
and `KernelPeerCredentials` explicitly documented as transport evidence rather
than an ENML identity. The interface is *ready*. Only `channel_linux.cpp` is
coupled, and a `channel_cookie.cpp` beside it is a small change. It would also
be unbuildable and untestable, because nothing could link it, load it, or run
it.

**Adding that seam now would repeat the mistake this project already documented
one increment ago.** `docs/M7_2_DELINUX.md`'s third-party-library section says of
the text backend: a seam with nothing behind it is not a capability. An IPC
substrate seam with no runnable Cookie process behind it is the same shape.

## The real prerequisite chain

In order, because each genuinely needs the one above it:

1. **Address spaces creatable and destroyable after boot** — M7.11, in
   progress. Without this there is no container for a process.
2. **A userland runtime**: syscall stubs for the kernel ABI, a program image
   format the loader and the build agree on, and a loader that places an image
   into an address space created in (1). This does not exist in any form and is
   not on the roadmap under any name.
3. **One process that is a compiled program rather than hand-written words** —
   the first honest end-to-end proof, and the point at which "Cookie runs
   software" becomes true.
4. **`channel_cookie.cpp` behind the already-neutral header**, then the rest of
   `docs/M7_2_DELINUX.md`'s order as written.

Steps 1–3 are the missing milestone. It now has a name and a design:
`docs/M7_12_FIRST_PROGRAM.md`. Step 4 is what the de-Linux plan describes,
and it is the *fourth* thing, not the first.

## What this changes

Nothing about the destination and everything about the sequence.

- `docs/M7_2_DELINUX.md`'s order stays correct **within** the migration and is
  not the first work.
- The bounded-receive work (M7.2, complete) was still the right thing to build:
  `docs/M7_2_SERVER_LOOP.md` established that no service loop was expressible on
  the kernel ABI at all, and that was true independently of whether a service
  could run. Fixing the ABI was a prerequisite of a prerequisite.
- **"18 of 18 files depend on Linux" should stop being reported as migration
  progress.** It is not a countdown that is stalled; it is a number that cannot
  move until a milestone that is not written down exists. Reporting it as
  progress-in-waiting overstates how close the migration is.

## The claim to keep making

Cookie is off Linux when the tree builds and its gates pass with no Linux
headers anywhere, on the emulated reference platform, with the Cookie Kernel
underneath. Until then Linux is the development host.

To that, this document adds a second: **Cookie cannot host anything at all
today.** The first claim is about a destination; this one is about whether the
vehicle exists. Both are owed, and the second is owed first.
