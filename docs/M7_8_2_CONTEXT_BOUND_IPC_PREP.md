# M7.8.2 — Context-Bound IPC Preparation

Status: PREPARATORY ONLY. Implementation remains locked until M7.8.1 demonstrates its exit gate.

## Purpose

M7.8.2 removes recyclable `ThreadId` as the security principal from IPC state. Where authorization or ownership matters, IPC must consume and record `ExecutionAuthority` = `(ThreadId, AddressSpaceIdentity)`, where `AddressSpaceIdentity` is the software `(slot, generation)` lifetime and deliberately excludes the recyclable hardware ASID.

The rendezvous/scheduler layer may continue to use `ThreadId` for mechanics. Security decisions must not.

## Existing risk surface

The current IPC endpoint implementation still stores or compares raw `ThreadId` in the following authority-bearing state:

- endpoint owner/server identity;
- pending caller/server relationship;
- reply seal caller/server identity;
- completed reply recipient;
- endpoint capability lookup holder;
- reply lookup by server + transaction;
- release/retirement helpers where process lifetime is inferred from numeric thread identity.

This is safe only while ThreadId cannot be recycled across a software address-space lifetime. M7.8 explicitly removes that assumption.

## Target authority model

### Endpoint ownership

`EndpointSlot::server` becomes `ExecutionAuthority server` for process-facing endpoints. Endpoint create/retire/receive operations accept the resolved live authority. Endpoint generation remains independently checked so endpoint-slot reuse and process-generation reuse are both fail-closed.

### Capability authorization

Add a context-bound `endpoint_for_capability(ExecutionAuthority, ...)` path that calls `CapabilityTable::holds(ExecutionAuthority, capability)` and rejects legacy unbound capabilities for process-facing IPC where required by the milestone contract. Do not compare only `authority.thread`.

Legacy ThreadId IPC functions may remain temporarily only for kernel-internal compatibility and must fail closed when presented with context-bound capability state.

### Pending calls

`PendingSlot` records the exact caller and server `ExecutionAuthority`. Mechanical rendezvous calls use `.thread`, but subsequent security comparisons use the complete authority object.

A pending call created by `(thread=7, generation=3)` must not be discoverable or consumable by `(thread=7, generation=4)`.

### Reply seals

`IpcReplySeal` records caller and server execution authorities. A seal is valid only when both authorities are valid, the endpoint generation matches, and the replying server's exact authority equals the seal server.

Transaction IDs are not principals and are insufficient alone to authorize replies.

### Completed replies

`CompletedSlot` records the exact caller `ExecutionAuthority`. `take_reply` and `reply_available` consume the exact live caller authority. Reusing the numeric thread ID must not expose a reply completed for an earlier generation.

### Teardown

M7.8.2 should provide exact-authority invalidation helpers, while the administrative numeric-thread teardown remains available for kernel destruction paths and deliberately removes all incarnations associated with that thread slot. M7.8.4 will later unify ordering across IPC, capabilities, translations, continuations, interrupts, and scheduler state.

## API shape

Preferred process-facing overloads:

- `create(ExecutionAuthority server)`
- `retire(ExecutionAuthority server, IpcEndpoint, Rendezvous&)`
- `send(ExecutionAuthority caller, CapabilityId, const CapabilityTable&, Rendezvous&, IpcEnvelope)`
- `receive(ExecutionAuthority server, CapabilityId, const CapabilityTable&, Rendezvous&)`
- `reply(ExecutionAuthority server, const IpcReplySeal&, Rendezvous&, IpcEnvelope)`
- `reply_transaction(ExecutionAuthority server, IpcTransactionId, Rendezvous&, IpcEnvelope)`
- `take_reply(ExecutionAuthority caller)`
- `reply_available(ExecutionAuthority caller)`
- `release_authority(ExecutionAuthority, Rendezvous&)`

Do not remove the thread-based mechanical rendezvous interface in this milestone. That would mix scheduler mechanics with security-principal migration and expand the review surface unnecessarily.

## Adversarial exit tests

M7.8.2 is not complete until tests prove all of the following:

1. A capability held by authority A cannot authorize send/receive under authority B with the same `ThreadId` but another address-space generation.
2. An endpoint owned by authority A cannot be received from, retired, or replied for by authority B with the same numeric thread.
3. A pending call from authority A is invisible to a later incarnation of the same thread slot.
4. A reply seal minted for authority A cannot be used by a recycled server generation.
5. A completed reply for authority A cannot be collected by a recycled caller generation.
6. `reply_transaction` validates the full server authority, not only transaction ID + numeric server thread.
7. Endpoint slot recycling plus thread/ASID recycling cannot resurrect a stale transaction relationship.
8. Exact-authority teardown removes pending/reply/completed state for that authority without granting anything to a subsequent generation.
9. Legacy ThreadId-only IPC fails closed when it encounters a context-bound capability.
10. Existing endpoint-generation stale-object tests continue to pass.

## Non-goals

- trusted syscall caller resolution (M7.8.3);
- unified teardown ordering/torture loops (M7.8.4);
- user VM lifecycle (M7.9);
- SMP/TLB shootdown correctness (M7.10);
- native service bootstrap/de-Linux implementation (P8).

## Review invariant

Every security comparison in IPC must answer: "Which software execution lifetime owns this authority?" If the code can answer only with a numeric `ThreadId`, the M7.8.2 migration is incomplete.
