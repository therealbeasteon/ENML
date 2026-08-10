# M3.2 — bounded collection publication transport

This slice moves ENML collection publication beyond the in-process callback seam without freezing a broad public widget ABI or exposing implementation-owned function pointers.

## Layering

The existing `core/osui` collection model remains the semantic source of truth:

`CollectionDataSnapshot`
→ stable `CollectionItemKey`
→ bounded `CollectionContentWindow`
→ bounded `CollectionChangeSet`.

`core/oscollection` sits above `osui` and `osipc` and defines versioned records plus a private pull-only session channel for those already-stabilized semantics. `osui` therefore stays independent of IPC and the renderer still consumes semantic collection state rather than transport-specific objects.

## Version 1 records

The first transport revision provides bounded codecs for:

- collection snapshot revision + logical item count;
- one ordered bounded change set between two revisions;
- one materialized semantic content window with stable keys, primary/secondary labels and enabled/selected state.

The format is explicitly versioned and size-prefixed. It carries no pointers, allocator state, C++ object addresses, font/render data or application-selected native descriptors.

## Private collection session

`CollectionSessionId` binds requests to one private collection capability. `CollectionSessionServer` owns the producer-side `CollectionDataSourceBackend`/`CollectionChangeSourceBackend`; those callback pointers never cross the channel. `CollectionSessionClient` receives only the private endpoint plus session id from a trusted outer lifecycle layer.

The session namespace supports exactly three synchronous operations:

1. snapshot — sample the current revision and logical item count;
2. changes-since — pull one bounded transition from a known older revision;
3. content-window — request semantic content only for one already-planned materialized window at an exact captured revision.

The server validates the session on every request. Content publication samples the live producer snapshot again and rejects a request whose captured revision is stale before any mixed-revision row can be returned.

## Flow control and power behavior

Version 1 is deliberately **pull-only**. The producer never pushes mutation events, never accumulates a notification queue and never owns a collection polling/prefetch worker.

A consumer samples a snapshot when collection state is relevant. If the revision did not change, there is no change request. If it changed, the consumer may request one bounded change transition and then request only the current materialized content window. A source that cannot summarize its mutation history within the existing 16-change bound can use the existing reset transition semantics instead of creating a backlog.

The RPC API is synchronous and request/response only; no oneway/event/cancellable/handle-bearing frames are accepted by the private session server. This makes back-pressure explicit: work exists only while a consumer has issued a bounded request.

## Bounds

The transport preserves the existing M3.2 limits:

- at most 1,000,000 logical collection items;
- at most 16 changes in one change set;
- at most 32 materialized items in one content window;
- at most 160 UTF-8 bytes per semantic label;
- the maximum content-window record remains well below the existing 64 KiB inline IPC payload ceiling.

Encoding, decoding and session dispatch use caller-owned buffers and introduce no background worker, prefetch thread or unbounded queue.

## Validation

Snapshot records reject zero revisions and item counts above the collection ceiling.

Change-set records are revalidated through `collection_change_set_valid()`, including revision identity, bounded change count, sequential index semantics, range validity and final item-count consistency. Unknown change kinds fail closed.

Content-window records validate nonzero revision, materialized/window-count agreement, collection index bounds, nonzero bounded item extent for nonempty windows, exact sequential item indices, nonzero unique stable keys, valid semantic labels and known state flags only.

The private session adds request-header validation, nonzero/exact session checks, response/request revision agreement, materialized-window echo validation and current-snapshot checks. For a nonempty collection, the requested content extent must match `item_count × item_extent`; stale revisions return `stale_collection_snapshot` rather than mixing identities from different generations.

## Why this is not the final public UI ABI

This transport intentionally serializes only semantics already proven in-process. It does not freeze a complete app-facing list/widget framework, custom row layout language or renderer contract.

A later OSIDL/public API may wrap these records after lifecycle ownership and product-facing collection semantics settle. The important invariants are now established: cross-process collection publication is record/message based, session-bound, pull-driven and bounded rather than callback-pointer based or producer-push queued.

## Remaining work

The remaining collection integration is the trusted outer lifecycle handoff that decides which application/runtime receives each private collection endpoint, plus eventual OSIDL/public semantic API packaging. Those layers must preserve the pull-only session and exact revision/key semantics rather than adding an unbounded mutation stream.
