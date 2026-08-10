# M3.2 — bounded collection publication transport and lifecycle

This slice moves ENML collection publication beyond the in-process callback seam without freezing a broad public widget ABI or exposing implementation-owned function pointers.

## Layering

The existing `core/osui` collection model remains the semantic source of truth:

`CollectionDataSnapshot`
→ stable `CollectionItemKey`
→ bounded `CollectionContentWindow`
→ bounded `CollectionChangeSet`.

`core/oscollection` sits above `osui` and `osipc` and defines versioned records plus a private pull-only session channel for those already-stabilized semantics. `osui` therefore stays independent of IPC and the renderer still consumes semantic collection state rather than transport-specific objects.

The trusted outer lifecycle is now:

`authenticated application runtime session`
→ App Manager mints bounded collection capability
→ application receives producer endpoint + nonzero `CollectionSessionId`
→ App Manager retains paired consumer endpoint in the exact live `PeerIdentity`
→ trusted collection consumer makes one exact app + session claim
→ `CollectionSessionClient` pulls bounded semantic state.

## Version 1 records

The first transport revision provides bounded codecs for:

- collection snapshot revision + logical item count;
- one ordered bounded change set between two revisions;
- one materialized semantic content window with stable keys, primary/secondary labels and enabled/selected state.

The format is explicitly versioned and size-prefixed. It carries no pointers, allocator state, C++ object addresses, font/render data or application-selected native descriptors.

## Private collection session

`CollectionSessionId` binds requests to one private collection capability. `CollectionSessionServer` owns the producer-side `CollectionDataSourceBackend`/`CollectionChangeSourceBackend`; those callback pointers never cross the channel. `CollectionSessionClient` receives only the private endpoint plus session id from trusted lifecycle state.

The private per-session service id is `0x0000F016`. This intentionally no longer shares the `0x0000F014` identifier used by the supervised accessibility service, even though both protocols are capability-local and were not technically routable through one another.

The session namespace supports exactly three synchronous operations:

1. snapshot — sample the current revision and logical item count;
2. changes-since — pull one bounded transition from a known older revision;
3. content-window — request semantic content only for one already-planned materialized window at an exact captured revision.

The server validates the session on every request. Content publication samples the live producer snapshot again and rejects a request whose captured revision is stale before any mixed-revision row can be returned.

## Authenticated application lifecycle ownership

An application creates a collection capability only through its already-authenticated App Manager runtime session. `PlatformServiceSession::acquire_collection()` sends the same empty versioned capability request shape used by other runtime-owned capabilities.

The request deliberately contains **no**:

- target `PeerIdentity` or `ApplicationInstanceId`;
- consumer principal;
- caller-selected session id;
- native descriptor;
- collection callback pointer or container address.

App Manager first validates the request's kernel `SCM_CREDENTIALS` against the `ServiceBroker` identity already bound to the live application instance. Only then may it allocate a channel pair and monotonic nonzero collection session id.

Each live application instance has a fixed capacity of **8 unclaimed collection sessions**. A full slot set returns an explicit capacity error rather than growing a map or dropping an older collection implicitly. Globally, session ids come from App Manager's monotonic counter and zero is reserved as invalid.

The application receives one endpoint and the session id. App Manager retains the paired consumer endpoint inside the exact instance slot. Process death/instance teardown therefore destroys still-unclaimed capabilities through ordinary ownership cleanup rather than a global collector.

## Trusted consumer handoff

`ApplicationManager::take_collection_endpoint()` is the one-shot internal authority seam for the consumer side. The caller must already be a trusted runtime/supervisor-resolved identity with `collection_consumer_principal`; authorization is checked before target/session lookup to avoid turning errors into an application/session enumeration oracle.

The claim requires both the **exact live application `PeerIdentity`** and the **exact runtime-minted session id**. A wrong session does not fall through to another collection. A successful claim moves the consumer channel out of App Manager and clears the slot, so a second claim of the same session fails.

This method is deliberately not a public application RPC. A future separate UI-host process must wrap this seam with the same kernel-credential authentication discipline used by accessibility/input rather than accepting a serialized caller principal.

## Flow control and power behavior

Version 1 is deliberately **pull-only**. The producer never pushes mutation events, never accumulates a notification queue and never owns a collection polling/prefetch worker.

A consumer samples a snapshot when collection state is relevant. If the revision did not change, there is no change request. If it changed, the consumer may request one bounded change transition and then request only the current materialized content window. A source that cannot summarize its mutation history within the existing 16-change bound can use the existing reset transition semantics instead of creating a backlog.

The RPC API is synchronous and request/response only; no oneway/event/cancellable/handle-bearing frames are accepted by the private session server. This makes back-pressure explicit: work exists only while a consumer has issued a bounded request.

## Bounds

The transport/lifecycle preserves the current M3.2 limits:

- at most 1,000,000 logical collection items;
- at most 16 changes in one change set;
- at most 32 materialized items in one content window;
- at most 160 UTF-8 bytes per semantic label;
- at most 8 unclaimed collection session capabilities per live application instance;
- the maximum content-window record remains well below the existing 64 KiB inline IPC payload ceiling.

Encoding, decoding and session dispatch use caller-owned buffers and introduce no background worker, prefetch thread or unbounded queue.

## Validation

Snapshot records reject zero revisions and item counts above the collection ceiling.

Change-set records are revalidated through `collection_change_set_valid()`, including revision identity, bounded change count, sequential index semantics, range validity and final item-count consistency. Unknown change kinds fail closed.

Content-window records validate nonzero revision, materialized/window-count agreement, collection index bounds, nonzero bounded item extent for nonempty windows, exact sequential item indices, nonzero unique stable keys, valid semantic labels and known state flags only.

The private session adds request-header validation, nonzero/exact session checks, response/request revision agreement, materialized-window echo validation and current-snapshot checks. For a nonempty collection, the requested content extent must match `item_count × item_extent`; stale revisions return `stale_collection_snapshot` rather than mixing identities from different generations.

The runtime-session unit test now proves the new collection capability operation transfers exactly one channel plus a nonzero session id. The full App Manager runtime integration fixture proves that a real launched application requests the capability over its authenticated post-READY channel, publishes a real `CollectionSessionServer`, an ordinary application principal cannot claim the consumer side, a wrong session id cannot redirect the claim, the trusted exact-owner consumer obtains a real snapshot, and replaying the one-shot claim fails. The same fixture remains part of the Service Broker validation matrix, including native AArch64 and sanitizer runs.

## Why this is not the final public UI ABI

The transport intentionally serializes only semantics already proven in-process. It does not freeze a complete app-facing list/widget framework, custom row layout language or renderer contract.

A later OSIDL/public API may wrap these records after product-facing collection semantics settle. The important invariants are now established: cross-process collection publication is record/message based, lifecycle-bound, session-bound, pull-driven and bounded rather than callback-pointer based or producer-push queued.

## Remaining work

Collection lifecycle ownership is no longer an unbounded/ambient handoff. Remaining collection work is primarily public semantic API/OSIDL packaging and, if collection consumption moves into a separate privileged UI-host process, an authenticated kernel-credential control wrapper around the existing one-shot consumer claim seam.
