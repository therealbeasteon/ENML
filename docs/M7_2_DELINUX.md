# M7.2 - Getting off the Linux kernel

The instruction is to leave Linux completely. This document is the map, and the
gate beside it is what stops the map going stale.

## The measured surface

Nineteen files in `core/` and `system/` depend on Linux. Not nine - that was my
own count from a narrower search, and the gate corrected it before the number
reached a document. That is the point of having the gate rather than a prose
estimate.

`.github/scripts/check-linux-coupling.sh` freezes the list. A file may leave it;
nothing joins it without deleting a line, which is a decision someone has to
make deliberately and defend. The risk in a migration like this was never the
nineteen files already known and scheduled - it is the twentieth, added later by
someone reaching for a Linux facility because it was there. Each one is small
and reasonable alone, and together they are why ports never finish.

The check deliberately flags only headers that mean *talking to Linux*. Plain
POSIX that a Cookie libc will provide anyway is not coupling, and flagging it
would train people to ignore the check.

## What each dependency becomes

**IPC - the largest and most central.** `channel.hpp`, `channel_linux.cpp`,
`identity.hpp`, `service_session.hpp`, `broker.hpp`, `accessibility_control.hpp`,
`shell_lifecycle_control.hpp`, `runtime_session.cpp`.

Everything here rests on `SOCK_SEQPACKET` carrying `SCM_RIGHTS` and
`SCM_CREDENTIALS` - message boundaries, descriptor passing, and kernel-attested
peer credentials. The Cookie Kernel provides all three natively: the rendezvous
in M7.1a preserves message boundaries by construction, `capability_grant`
replaces descriptor passing, and the sender's identity is known to the kernel
rather than asserted in a control message. This is the one migration that
*simplifies* the code, because ancillary-data handling disappears entirely.

**Sandboxing.** `sandbox.hpp`, `sandbox_linux.cpp`, `substrate.hpp`,
`substrate_linux.cpp`.

seccomp, Landlock and `no_new_privs` all exist to claw back authority a
process was given by default. The Cookie Kernel has no ambient authority to
claw back - a thread holds the capabilities it was granted and nothing else, and
the ABI enforces that at the call boundary. Most of this layer does not port; it
is deleted. `SubstrateReport` becomes a statement about the Cookie Kernel's own
configuration rather than a probe of somebody else's.

**Process and supervision.** `process_authority.hpp`,
`process_authority_linux.cpp`, `supervisor.cpp`, `manager_linux.cpp`,
`bootstrap.hpp`.

`fork`/`exec`, `waitpid`, pidfds and signals become `address_space_create`,
`thread_create` and `thread_exit`, with death observed through the rendezvous:
a thread that exits already releases everyone blocked on it and tells them it
was a death rather than an answer. The supervisor's restart logic is unchanged
in shape.

**Display buffers.** `buffer_linux.cpp`, `compositor/src/service.cpp`.

Shared memory through `memfd` with seals becomes a mapping established by the
kernel `map` call and transferred as a capability. Sealing - the property that a
buffer's size cannot change under the compositor - becomes an attribute of the
capability instead of a filesystem trick.

## Order

**Amended 2026-08-14 (second time) — the order cannot start at all yet.**
`docs/M7_2_NO_DESTINATION.md` records the finding: not one of the eighteen files
can move, because Cookie has no userland to move them to. No syscall stubs, no
program format, no loader, no post-boot address space creation; the only code
that has ever run at EL0 is raw instruction words this boot routine writes into
pages by hand. Everything below remains correct about *what each dependency
becomes* and is the fourth thing to do, not the first.

**Amended 2026-08-14 — step 1 had an unnamed prerequisite.** The three
primitives listed above are real, but a server also has to *wait on many peers*,
and every service main loop in the tree does that with `poll()` over a
descriptor table while `KernelCall::receive` takes exactly one endpoint. No
service loop could be written on the current ABI. `docs/M7_2_SERVER_LOOP.md`
decides the shape — many clients send to one endpoint and the kernel attests who
called, so the multiplexing is deleted rather than ported — and names the one
genuine kernel gap that remains, a receive with a deadline. Note also that the
claim below about supervision being unchanged in shape does **not** extend to
servers: their loops change shape, and simplify.

The order is chosen so the tree stays green throughout, not by how much is left:

1. **IPC**, because everything else is layered on it and it is the one that gets
   simpler.
2. **Process and supervision**, which needs IPC to observe death.
3. **Display buffers**, which needs capability transfer.
4. **Sandboxing**, last, because most of it is deleted rather than ported and
   deleting it early would leave the Linux-hosted build unconfined while the
   rest of the migration is still in progress.

## The other dependency axis: third-party libraries

Added 2026-08-14. Everything above maps *kernel* coupling — the headers and
syscalls that mean talking to Linux. That is not the whole of "no external
dependence", and the second axis had no map at all.

Measured, not estimated. Four external packages appear in the build:

| Package | Where | Gate |
| --- | --- | --- |
| OpenSSL | `core/oskeys` | `EMNL_BUILD_OPENSSL_TEST_PROVIDER` |
| Freetype | `core/osui/platform/linux` | `EMNL_BUILD_LINUX_TEXT_BACKEND` |
| ICU (`uc`, `i18n`) | `core/osui/platform/linux` | `EMNL_BUILD_LINUX_TEXT_BACKEND` |
| HarfBuzz | `core/osui/platform/linux` | `EMNL_BUILD_LINUX_TEXT_BACKEND` |

**The good news is structural and was already right: every one of them is
opt-in, and the seams are in the correct place.** A default Cookie build links
none of them. `EMNL_BUILD_LINUX_TEXT_BACKEND` additionally hard-errors on a
non-Linux `CMAKE_SYSTEM_NAME`, so it cannot be switched on by accident on the
target. Behind each is an ENML-owned interface rather than a leaked vendor API:
`TextShaperBackend`/`FontAwareParagraphShaperBackend` are function-pointer
boundaries in `core/osui/include/os/ui/text.hpp`, and `PersistentKeyProvider`
is an interface whose OpenSSL implementation `AGENTS.md` already labels
test-only. Nothing has to be re-architected to remove them.

**The bad news is that a seam with nothing behind it is not a capability.** On
Cookie today there is no text shaper, no font rasterizer and no Unicode
character database — `os::ui::error::text_shaper_unavailable` is the honest
answer the code already returns. A phone that cannot render text is not a phone,
so this is on the critical path and it is larger than the whole IPC migration:
shaping, rasterization and Unicode tables are three substantial subsystems, and
each is a parsing surface handling untrusted input at a trust boundary, which is
precisely the category `docs/M4_5_FUZZING_DEPTH.md` exists for.

The decisions are not made here and should not be made casually. What this
section fixes is that they were not previously *recorded as owed*:

- **Font rasterization.** A rasterizer parses attacker-supplied font files. It
  is a classic remote-code-execution surface, and "use the well-tested one" is
  a real argument against writing one.
- **Text shaping.** Complex-script shaping is where correctness and cultural
  adequacy live. Getting this wrong is not a security bug, it is a product that
  cannot be used in most of the world.
- **Unicode data.** Tables, not code — the most mechanical to own and the
  easiest to keep current, and a plausible first target.
- **Key provider.** Distinct from the other three and already scheduled
  elsewhere: `AGENTS.md` requires a production TPM/TEE/HSM provider and forbids
  faking one in a kernel milestone. Phases 4–6 own it.

Whether Cookie writes these, vendors them as reviewed source inside the trust
boundary, or confines them in their own address spaces behind the existing
seams, is a decision this document now demands rather than one the build makes
by default.

## What "completely off Linux" will mean

The gate reaching zero is necessary and not sufficient. Cookie is off Linux when
the tree builds and its gates pass with no Linux headers anywhere, on the
emulated reference platform, with the Cookie Kernel underneath. Until then Linux
remains the development host, which is a different claim from being the
substrate and should not be reported as the same thing.

And "off Linux" is still not the same as "no external dependence". The
coupling gate counts headers; it does not count linked libraries, and the four
above would not move it by one file. A Cookie that boots its own kernel and
still cannot draw a glyph without Freetype has met the first claim and not the
second. Both are owed.
