# M3.2 — bounded collection publication transport

This slice moves ENML collection publication one step beyond the in-process callback seam without freezing a broad public widget ABI or exposing implementation-owned function pointers.

## Layering

The existing `core/osui` collection model remains the semantic source of truth:

`CollectionDataSnapshot`
→ stable `CollectionItemKey`
→ bounded `CollectionContentWindow`
→ bounded `CollectionChangeSet`.

`core/oscollection` sits above `osui` and `osipc` and defines versioned record codecs for those already-stabilized semantics. `osui` therefore stays independent of IPC and the renderer still consumes semantic collection state rather than transport-specific objects.

## Version 1 records

The first transport revision provides bounded codecs for:

- collection snapshot revision + logical item count;
- one ordered bounded change set between two revisions;
- one materialized semantic content window with stable keys, primary/secondary labels and enabled/selected state.

The format is explicitly versioned and size-prefixed. It carries no pointers, allocator state, C++ object addresses, font/render data or application-selected native descriptors.

## Bounds

The transport preserves the existing M3.2 limits:

- at most 1,000,000 logical collection items;
- at most 16 changes in one change set;
- at most 32 materialized items in one content window;
- at most 160 UTF-8 bytes per semantic label;
- the maximum content-window record remains well below the existing 64 KiB inline IPC payload ceiling.

Encoding and decoding use caller-owned buffers and introduce no background worker, prefetch thread or unbounded queue.

## Validation

Snapshot records reject zero revisions and item counts above the collection ceiling.

Change-set records are revalidated through `collection_change_set_valid()`, including revision identity, bounded change count, sequential index semantics, range validity and final item-count consistency. Unknown change kinds fail closed.

Content-window records validate:

- nonzero revision;
- materialized count and window-count agreement;
- collection index bounds;
- nonzero item extent for nonempty windows;
- exact sequential item indices for the advertised window;
- nonzero, unique stable item keys;
- valid nonempty primary semantic labels;
- valid optional secondary UTF-8 labels;
- known content-state flags only.

Malformed records therefore cannot silently substitute a different stable key or publish a row under an unrelated materialized index.

## Why this is not the final public UI ABI

This transport intentionally serializes only the semantics already proven in-process. It does not freeze a complete app-facing list/widget framework, custom row layout language or renderer contract.

A later OSIDL/public API may wrap these record semantics once application/session ownership, flow control and mutation delivery are finalized. The important invariant is already established: cross-process collection publication is record/message based and bounded rather than callback-pointer based.

## Remaining work

Before M3.2 leaves draft, collection transport still needs an owning application/session channel and explicit flow-control/update-delivery policy. A producer must not be able to create an unbounded mutation backlog, and stale revisions must still fail closed across that process boundary.
