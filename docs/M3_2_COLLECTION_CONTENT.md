# M3.2 — bounded collection content publication

This slice extends ENML's collection virtualization from stable identity/recycling into bounded semantic content publication without freezing the eventual public application ABI around in-process callbacks.

## Revision and identity contract

A materialization pass still begins with one `CollectionDataSnapshot` containing a nonzero `CollectionRevision` and bounded item count.

For each item in the fixed materialized window:

1. `item_key_at()` resolves the stable `CollectionItemKey` for the captured revision;
2. zero/duplicate keys are rejected before content publication;
3. `item_content_at()` receives that exact validated revision, index and stable key;
4. a backend that can no longer serve that revision must refuse the request, causing the whole publication to fail as stale rather than mixing collection states.

`CollectionContentWindow` therefore contains at most `max_materialized_collection_items` rows and names the exact revision from which every row was produced.

## Initial semantic content shape

`CollectionItemContent` is deliberately small:

- required validated primary semantic label;
- optional validated secondary semantic label;
- enabled state;
- selected state.

The primary label provides a minimum meaningful/accessibility description for a materialized item. The optional secondary label supports a common two-line information hierarchy without exposing typography, font files, glyphs, RGB values, textures or vendor widget types.

This is not intended to become the final universal custom-row API. Richer row composition should use the later public semantic UI/OSIDL model after its ownership and mutation semantics are stable.

## Recycler consistency

`build_collection_content_window()` reuses `build_collection_recycle_request()` before asking for content. Content and recycler slots therefore derive from the same validated stable keys for one captured revision.

This prevents a source from publishing visible/accessibility content under a different logical identity than the item whose semantic slot, focus state or selection continuity is being retained.

## Resource and power discipline

Publication is bounded to the already-materialized collection window. ENML does not construct semantic content for a million logical rows, prefetch an unbounded backing model, create a collection worker pool or poll the source while idle.

A distant mutation that does not affect the visible window can continue to avoid unnecessary rematerialization through the existing bounded change-set logic.

## Security and ownership

The current callbacks are an in-process implementation seam only. They are not the application ABI.

The eventual cross-process collection protocol should carry explicit revision, index/key and bounded semantic content records over authenticated IPC. It must not expose implementation pointers, application container addresses, renderer resources or unrestricted shared memory as the normal list-data interface.

Malformed UTF-8, empty primary labels, duplicate/zero keys and stale revisions fail before publication is accepted.

## Not yet claimed

This slice does not yet provide:

- the public collection OSIDL messages;
- arbitrary custom row subtrees;
- asynchronous remote paging/prefetch;
- image/media payload publication;
- editable collection cells;
- drag/reorder gestures;
- persistence or database ownership.

Those features should be layered onto the stable revision/key/materialization contract rather than bypassing it.
