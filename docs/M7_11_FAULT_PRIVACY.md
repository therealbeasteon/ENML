# M7.11 — What a pager is allowed to know

**Status: mechanism landed, not yet wired into the fault path.**
`FaultRegionTable` (`core/oskernel/include/os/kernel/fault_region.hpp`) exists
and is tested. `cookie_aarch64_unhandled_fault` does not consult it yet; that is
the next increment, and it is deliberately separate because this document is the
decision and the wiring is only its consequence.

## The problem this exists for

`docs/M7_11_MEMORY.md` decided that the fault path delivers to a userland pager:

> `ESR_EL1` and `FAR_EL1` decoded, the fault classified, and the result
> delivered to a userland pager over the IPC machinery M7.6a/M7.8 already
> built… The kernel decides *what happened*; it does not decide what to do
> about it.

That is the right split and Cookie keeps it. But taken literally — deliver the
decoded fault, `FAR_EL1` included — it builds a **controlled-channel attack into
the design**, and Cookie would be handing out the oracle rather than suffering it.

Xu, Cui and Peinado (*Controlled-Channel Attacks*, IEEE S&P 2015) showed that an
adversary who observes a victim's *page-fault sequence* recovers complete text
documents and the outlines of JPEG images from unmodified application libraries.
Van Bulck et al. (USENIX Security 2017) made the same channel stealthier through
page-table access bits. The resolution is 4 KiB and it is still devastating,
because secret-dependent control flow and secret-dependent data access both show
up in which page is touched next. SGX masks the low 12 bits of the faulting
address before the OS sees it; that did not help.

Two capabilities make the attack work:

1. **The adversary learns which address faulted.**
2. **The adversary can re-arm** — revoke access again and be told again — so it
   collects a *trace* rather than a single event.

A microkernel that exports paging to userland gives a pager both, by
construction. L4 and seL4 synthesise a fault message on the faulting thread's
behalf carrying the faulting address, and the pager owns the frames and
therefore the mappings, so it may unmap and be asked again as often as it likes.
That is not a defect in those kernels; it follows from a threat model in which
the pager is part of the trusted computing base.

**Cookie cannot make that assumption.** The whole architecture says userland
services are untrusted and the kernel is the only thing that is. A memory
manager is a userland service. So in Cookie the pager is a controlled-channel
adversary *by definition*, and the design doc's own fault path would have made
every process's access pattern legible to it.

## Why Cookie can close this and the enclave literature could not

Autarky (Orenbach et al., EuroSys 2020) closes controlled channels for SGX
enclaves and needs **backward-compatible ISA modifications** to do it, because
in the enclave threat model the kernel *is* the adversary — there is no trusted
software below the enclave to redact anything, so the redaction has to be
silicon.

Cookie's boundary is drawn in a different place, and that is the whole
opportunity: the kernel is trusted and the pager is not. The redaction point
already exists, runs on every fault, and needs no hardware that does not ship
today. What is a hardware problem for an enclave is a **software design choice**
for a microkernel that never trusted its pager in the first place.

This is not Autarky's mechanism and does not use its parts — no ORAM, no page
clusters, no ISA change, and no enclave. The shared observation is only that a
page-fault trace is a disclosure.

## The decision

**A pager is told which region needs backing. It is never told an address, and
it is never told twice.**

### 1. Region, not address

`FaultReport` has no faulting-address field, and adding one would undo the
design. It carries a `FaultRegionId` — a region the faulting process itself
declared the extent of.

The consequence is that **the resolution of anything a pager can infer is the
process's decision, not the hardware's page size.** A process that declares one
64 MiB heap region gives its pager 64 MiB of resolution. This inverts the usual
arrangement, where the page size is an architectural constant the application
cannot influence and the attacker gets 4 KiB for free.

### 2. No re-arm, because the pager never holds unmap authority

A region is deliverable only on a lifecycle transition it has not already made,
and each transition is announced exactly once for the region's whole life:

```
unbacked ──(one report)──> backed_shared ──(one report, on write)──> backed_private
```

After that, faults in the region are not questions: they are permission
violations, and the answer is to terminate the thread, not to ask userland.

The structural half matters more than the counter. `mark_backed` **cannot return
a region to `unbacked`** — there is no interface for it, and the attempt is a
typed error. The pager *supplies backing*; the kernel *installs translations*.
Separating those two is what removes the re-arm primitive, and Cookie can
separate them only because `docs/M7_11_MEMORY.md` already decided that memory
authority is a capability a process holds rather than something the pager owns.
The no-kernel-heap decision paid for this without knowing it.

### 3. `sealed`: memory that cannot generate a question

A region declared `sealed` is never reported to any pager, at any granularity,
ever. It must be backed before it is reachable, and a fault in it terminates the
faulting thread.

This is where key material, plaintext buffers and any secret-dependent index
belong. `AGENTS.md` already forbids a secret from influencing a comparison's
timing, a divisor, or an alignment. `sealed` is the same rule reaching one level
further out:

> **A secret must never influence which page faults.**

A sealed region cannot be demand-paged, cannot be swapped, and cannot carry an
access pattern out of the address space, because there is no mechanism by which
anything outside the kernel is informed of an access to it.

### 4. Undeclared memory is nobody's question

A fault outside every declared region is never reported. Telling a pager that an
address it has no responsibility for was touched is a channel with no
corresponding service — pure disclosure, zero function.

## What this does not close

Stated plainly, because a security claim that omits its residue is worse than no
claim.

- **First touch is observable.** The pager learns that a region was first
  touched, and when. With coarse regions that is a weak signal, but it is a
  signal.
- **Order across regions is observable.** A process with regions A, B, C leaks
  the order it first touches them. A process that wants this hidden must
  pre-touch or seal.
- **Timing is not addressed here at all.** The *time* of a first touch is a
  channel, and rate-shaping fault delivery is a separate question this document
  does not answer. `docs/M6_2_TIME_PROTECTION.md` owns that axis.
- **Nothing here defends against a malicious *kernel*.** That is not Cookie's
  threat model; the kernel is the thing being kept small enough to audit.

The honest summary is a **capacity reduction, not a closure**: from an unbounded
trace at 4 KiB resolution, to at most two events per region for the region's
entire lifetime, at a granularity the process chooses. `sealed` takes it to zero
for the memory that cannot afford even that.

## Cost, paid knowingly

- **No page-granular copy-on-write.** A region transitions to private as a
  whole. CoW at page granularity is exactly a mechanism for reporting a
  fine-grained write pattern to userland, repeatedly. Cookie declines it and
  pays in memory.
- **No page-granular demand paging.** Backing arrives per region. A process that
  wants finer lazy behaviour declares finer regions and accepts that it is
  choosing to disclose more — which is the right place for that decision.
- **`max_fault_regions` is 32 per address space.** Fixed, like everything else
  the kernel holds, per the no-dynamic-allocation decision. If that proves too
  few it is a number to argue about in review, not a structure to make dynamic.
- **The SDK carries the ergonomics.** A process must declare regions before it
  touches them. This lands on Phase 7 alongside the explicit memory manager the
  no-kernel-heap decision already put there.

## Why the decision is made now

The same reason the no-kernel-heap decision was made before anything allocated.
Once a pager protocol carries a faulting address, every pager depends on it, and
removing the field is a flag day across the whole memory manager. Once it does
not, nothing ever asks. This is a field in a structure that does not exist yet —
the cheapest moment it will ever be.

## References consulted

- Xu, Cui, Peinado. *Controlled-Channel Attacks: Deterministic Side Channels for
  Untrusted Operating Systems.* IEEE S&P 2015.
- Van Bulck et al. *Telling Your Secrets Without Page Faults: Stealthy Page
  Table-Based Attacks on Enclaved Execution.* USENIX Security 2017.
- Orenbach et al. *Autarky: Closing Controlled Channels with Self-Paging
  Enclaves.* EuroSys 2020.
- seL4 / L4 fault-IPC design, for the arrangement Cookie is deliberately not
  adopting.

References teach principles; ENML determines implementation. Nothing above is
imported: the threat is taken, the mechanism is Cookie's.
