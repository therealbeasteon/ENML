# M7.1c - Capability transfer

The fourth kernel responsibility, and the only one the references do not
demonstrate. `docs/M7_0_KERNEL.md` states why it is in the kernel rather than
above it: the whole system treats authority as an object that is granted,
brokered and revoked, and if moving one is not a kernel primitive it is a
convention - which is precisely what an attacker with a memory-safety bug
ignores.

Like the rendezvous, it is written as pure logic and tested on a development
host. `core/oskernel/include/os/kernel/capability.hpp` is the contract;
`tests/unit/kernel/capability_test.cpp` is what holds it.

## Which kind of capability system

The references describe two families. Capabilities can be **named by indices
into a kernel-held table**, unforgeable because the holder never touches the
representation; or they can be **protected by a one-way function**, held
anywhere and checked by recomputing it.

Cookie is in the first family, and the reason is worth stating because the
second is the more sophisticated-looking choice. Cryptographic capabilities
exist to solve a *distributed* trust problem: a server that cannot trust the
kernel holding the capability, because there are several kernels and a network
in between. A phone has one kernel and every party is behind it. Buying nothing,
the choice would cost two things - a cryptographic operation on a checking path
the mobile references specifically warn must not cost hundreds of clock cycles
on a battery-powered device, and both of the defects below.

## The two defects, and why Cookie cannot inherit them

The references do not present capability systems as unqualified successes. They
record two failure modes as properties of the approach in general.

**Revocation is all-or-nothing.** Where a capability is validated against a
check value stored with the object, revoking means changing that value, and in
one blow every outstanding capability for that object is invalidated. Taking
back what one holder was given means taking it back from everyone. The
literature states plainly that neither family allows selective revocation and
calls it a defect generally recognised in capability systems.

**Proliferation is uncontrolled.** Nothing stops a holder from copying a valid
capability to everyone it can reach. The references note that a kernel-managed
table is what solves this, and that a distributed scheme cannot.

Cookie cannot inherit either, and the reason is specific rather than
aspirational: M2.3 already claims deterministic revocation at the storage
service layer. A kernel weaker than the service built on top of it would make
that claim false from underneath, which is the worst kind of security
regression - one where the documentation stays true and the system stops being.

## What this milestone does about them

**Every capability records what it was derived from.** A grant creates a *child*
of the capability it came from. Revoking a capability removes it together with
its entire derivation subtree, and nothing else. That is selective revocation:
taking back what Bob was given also takes back whatever Bob passed on, and does
not touch what Carol was given from the same parent. The derivation tree is the
whole mechanism; there is no second registry to keep in step with it.

**Passing a capability on is itself a right.** A capability granted without
transferability is a leaf. Its holder can use it and can surrender it, and
cannot make anyone else a holder. Whether a recipient becomes a distribution
point is therefore a decision its granter makes, rather than a property the
system hands out with every grant.

**Attenuation only ever attenuates.** A child's rights are a subset of its
parent's, enforced at the grant rather than trusted to the caller. This is the
same server-authoritative rights reduction M2.1 already applies one layer up,
and it is what makes every other rule here load-bearing: a grant that could add
rights would leave the rest decorative.

**Two parties may revoke, and no others.** The holder, because surrendering your
own authority must always be available - a thread that cannot drop a capability
cannot reduce its own attack surface. And the holder of the *parent*, because
that is what taking back a grant means. A sibling's holder has no say. That
asymmetry is the difference between selective revocation and the kind the
references describe as a defect.

**A dead thread holds nothing.** Exiting surrenders everything the thread held
and everything derived from it - including capabilities still held by live
threads, because that authority only ever existed on the strength of a thread
that no longer does. This is the same unrecoverable obligation the rendezvous
meets when it releases everyone blocked on a thread that dies, and the same rule
M6.2's partition ledger applies to a dead principal.

## Choices worth stating

**Rights are opaque to the kernel.** The kernel stores a bitmask, intersects it,
and never interprets a bit. Intersection is meaningful without knowing what the
bits mean, and that is exactly the split that lets attenuation be enforced by a
kernel with no opinion about storage or displays. The same applies to the object
identifier: stored, matched, handed back, never interpreted.

**Transferability is a separate field, not a reserved bit.** The kernel does
interpret this one, so it does not live in a namespace the kernel does not own.
A reserved bit inside the opaque mask would be the kernel quietly claiming part
of a service's vocabulary.

**Identifiers are 64-bit and never reused.** Unlike a thread id, these are minted
by the kernel rather than chosen by a caller, so nothing outside keeps them
distinct. A recycled identifier would let a stale reference resolve to whatever
authority happened to be minted next - the hazard the rendezvous avoids by
retaining exited thread slots. A 32-bit counter is reachable in weeks at a rate a
busy phone can sustain, so it is 64 bits, and exhaustion is still checked rather
than assumed away.

**The delegation chain has a ceiling.** Worth having on its own - a chain longer
than eight is one nobody can audit - but it also does structural work. Removing a
subtree takes that many propagation passes and no more, so the cost of a revoke
is a compile-time constant rather than a function of how much delegation an
attacker arranged first. That is the bounded-work-per-call rule from
`docs/M7_4_KERNEL_HARDENING.md` holding in the one operation here that could
plausibly have broken it.

**Removal uses no scratch memory.** Slots are marked in the table and cleared by
the same call. A kernel stack is not the place for a list whose length depends on
how much delegation happened, and there is no heap to put it in either - M7.4a's
no-kernel-heap rule is a security property, so an operation that needs a
temporary list is an operation that needs redesigning.

**The capability table does not consult the rendezvous.** Two state machines that
each know about the other are two state machines neither of which can be tested
alone. Surrendering a dead thread's capabilities is exposed as an operation the
layer that owns both calls at exit, not as a hidden coupling.

## What is not decided yet

**How a grant rides on a message.** `docs/M7_0_KERNEL.md` describes capability
transfer as moving authority as part of a message, and the state machine here is
deliberately message-agnostic: it defines what a legal transfer *is*, not when
one is submitted. Binding a grant to a completed rendezvous - so that authority
can only move along a conversation both parties are already in, the same way
reply is bound to a received message rather than to a thread id - is the natural
next step and belongs with the call entry path.

**What `CallAuthority::capability_control` gates.** In the frozen ABI table it is
the authority for both `capability_grant` and `capability_revoke`. This milestone
implements the stricter reading: `mint` - creating authority that did not
previously exist - is the operation that needs a separate authority, while grant
and revoke are gated on a capability the caller already holds, so that within
this state machine there is no privilege to escalate *to*. Reconciling the table
with that reading is an ABI question and is deliberately not settled here by
editing the table in passing. No call was added; the ceiling is untouched.

**Error-code overlap.** `errors::unknown_call`, `rendezvous_errors::` and
`capability_errors::` each number from one inside `ErrorDomain::kernel`, so a
code alone does not identify a condition. Every caller today knows which
operation it made, so nothing is currently wrong; it is recorded because the
first consumer that logs a bare domain and code will be wrong, and finding that
out from a log is expensive.
