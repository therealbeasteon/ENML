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

## What "completely off Linux" will mean

The gate reaching zero is necessary and not sufficient. Cookie is off Linux when
the tree builds and its gates pass with no Linux headers anywhere, on the
emulated reference platform, with the Cookie Kernel underneath. Until then Linux
remains the development host, which is a different claim from being the
substrate and should not be reported as the same thing.
