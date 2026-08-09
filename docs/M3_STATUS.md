# M3 Status

## M3.0 — display/compositor ownership foundation

Status: complete and merged.

Implemented:

- additive stable `ErrorDomain::display`
- no-exceptions/no-RTTI `core/osdisplay` library
- strong 64-bit `SurfaceId`
- bounded display size, safe insets, refresh timing and compositor scheduling margin
- four trusted surface roles: application, popup, system chrome, secure system
- exact `PeerIdentity` surface ownership
- distinct trusted shell and secure-UI principals
- one application root per live process in the initial phone model
- popup attachment restricted to an exact-process-owned application parent
- compositor-owned application stack groups; a popup cannot escape above its parent application group
- trusted-shell-only `activate_application()`; applications cannot self-promote or submit arbitrary global z values
- system chrome above app groups and secure-system UI above both
- 64-surface global table and eight-surface per-principal budget
- process-death surface revocation with no authority inheritance across fresh `ProcessId`
- bounded three-slot frame metadata and eight damage rectangles
- monotonically increasing frame sequence and replay rejection
- geometry mutation invalidates stale frame geometry
- frame timing/deadline calculation from display refresh policy
- deterministic bounded scene snapshot
- secure-system surfaces marked non-capturable
- trusted top-down hit testing over visible/input-enabled/framed surfaces
- GCC, Clang, ASan/UBSan and native AArch64 M3 display gates

See `docs/M3_0_DISPLAY_COMPOSITOR_FOUNDATION.md`.

## M3.1 — typed shared buffers + supervised compositor service

Status: complete and merged in PR #25.

Implemented:

- strong generation-scoped `BufferId` and `SurfaceId` namespaces
- generation bits derive from trusted Supervisor bootstrap state, never from application payloads
- stale semantic display IDs cannot alias a replacement compositor generation
- fixed 48-buffer global table and eight-buffer per-principal limit
- 24 MiB per-buffer, 48 MiB per-principal and 128 MiB global byte ceilings
- Linux memfd backing with CLOEXEC, exact size and grow/shrink/write seal policy
- move-only `SharedBufferLease` ownership
- exact `PeerIdentity` buffer ownership and process revocation
- frame submissions bind compositor-owned surface authority to semantic `BufferId`
- buffer release/revocation invalidates scene entries still presenting that buffer
- real supervised `system.compositor` service
- trusted identity publication through the existing pidfd-backed `IdentityRegistry`
- per-message `SCM_CREDENTIALS` validation before public compositor operations
- typed create-surface, allocate-buffer and frame-submit service operations
- exact Supervisor generation placed into `BootstrapRecordV1.boot_generation`
- restart integration proving old service channels stay dead, fresh endpoint reacquisition works, live application `PeerIdentity` remains unchanged, and old SurfaceId/BufferId values do not collide with new-generation objects
- M3 display CI builds/runs focused gates on GCC, Clang, ASan/UBSan and native AArch64

See `docs/M3_1_SHARED_BUFFER_COMPOSITOR_SERVICE.md`.

## M3.2 — bounded semantic UI tree + accessibility/design foundation

Status: implementation in progress on `m3-2-semantic-ui-foundation`.

Implemented in the current branch:

- additive `ErrorDomain::ui`
- new no-exceptions/no-RTTI `core/osui` library
- strong `UiNodeId` and separate `StyleTokenId`
- fixed 256-node semantic tree
- 32 direct children per node and depth limit 16
- fixed 160-byte validated UTF-8 semantic labels
- monotonic node IDs with no stale-ID reuse inside a live tree
- roles for root/container/text/image/button/toggle/text-field/list/list-item
- explicit visible/enabled/focused/selected/checked/pressed state
- typed activate/focus/toggle/set-text/select actions with role validation
- one focused semantic node at a time
- effective ancestor visibility checks for focus/actions/accessibility
- fixed-capacity accessibility projection from semantic nodes rather than pixels
- decorative accessibility-hidden grouping with descendant re-parenting
- fixed-point density-independent logical geometry (Q6: 64 units per logical dp)
- safe-inset-aware responsive single-pane/dual-pane list-detail layout
- explicit configurable responsive policy rather than device-class probing
- bounded collection virtualization planning for up to 1,000,000 logical items with materialization bounded by the semantic child budget
- deep-list 64-bit scroll/content extent with viewport-relative materialized item coordinates
- deterministic fixed-pool collection recycler retaining overlapping item slots and reusing the lowest free slots
- strong 64-bit `CollectionItemKey` identity separate from mutable collection index
- keyed recycler binding that preserves a materialized semantic slot across insertion/reordering when the logical item key remains stable
- strong `CollectionRevision` plus bounded `CollectionDataSnapshot` for one logical data-source generation
- revision-scoped collection data-source seam: snapshot capture plus stable-key lookup for only the current captured revision
- stale collection-revision rejection so one materialization pass cannot mix keys from two source states
- zero/duplicate-key and malformed-source rejection before recycler binding
- immutable renderer snapshot with revision tracking
- bounded renderer dirty/removal delta and full-resync fallback on bookkeeping overflow
- semantic design token table independent of concrete vendor colors/assets
- typography/spacing/color/simple-shape roles
- optical material roles for opaque/translucent/crystal/smoked/luminous treatment
- separate depth roles for flush/inset/raised/floating/hero hierarchy
- authored curve roles including continuous and asymmetric swept contours
- bounded contour resolution independent of vendor graphics/path APIs
- motion roles for micro/responsive/transition/reveal behavior
- multiple semantic accent/tint roles without fixed RGB ABI
- reduced-transparency, reduced-motion and high-contrast visual preference resolution
- explicit economy/balanced/full optical quality tiers that reduce rendering cost without changing semantic hierarchy or authored contour identity
- explicit renderer capability profile for alpha compositing, live backdrop filtering, spatial motion and bounded optical blur
- capability fallback that keeps ENML material/contour identity while degrading unsupported translucency, backdrop work, depth blur and spatial motion
- deterministic fixed-capacity `RenderCommandBuffer` lowering from validated renderer snapshots
- renderer-snapshot validation including unique IDs, one root, parent/depth consistency and bounded cycle rejection
- deterministic command ordering, effective ancestor visibility and semantic-only unstyled-node support
- explicit separation between accessibility labels and visible renderer text content
- platform-owned semantic font-family roles and bounded fallback chains without font paths or vendor family names in UI ABI
- renderer-private bounded shaped-text contract: at most 160 glyph records, 32 font/direction runs and 16 lines per semantic text value
- shaped-output validation against UTF-8 cluster boundaries, semantic fallback families, run direction, line partitioning and logical geometry bounds
- renderer-owned `TextShaperBackend` seam with validated `shape_text()` handoff and explicit unavailable/failed/malformed backend errors
- text measurement derived from validated shaped glyph advances rather than UTF-8 byte/code-point counts
- text scaling from 100% through 300% and minimum logical touch-target policy
- large-text tests proving row reflow and reduced visible collection window size
- phone/tablet recomposition tests preserving semantic node identity
- adversarial unit tests for malformed UTF-8, oversized/forged labels, stale node IDs, depth/child bounds, role/state/action misuse, focus transfer and accessibility projection
- responsive layout tests for phone/tablet-like viewports, safe insets, policy changes and invalid viewport/policy inputs
- collection-window/recycler/source tests for overscroll, deep scrolling, slot retention/reuse, stable-key mutation, revision advance/stale-snapshot rejection, duplicate/zero-key rejection, window limits and out-of-window access
- renderer snapshot/delta and resolved-command tests including quality/accessibility/capability fallback
- shaped-text tests including backend validation, multi-line measurement, non-monotonic RTL cluster order, invalid UTF-8 cluster boundaries, fallback-family rejection and extent limits
- dedicated GCC, Clang, ASan/UBSan and native AArch64 semantic-UI gates

The renderer-command, semantic font fallback, bounded shaping contract/backend seam, stable-key recycler and renderer capability work through commit `f4d30bd54b2494aabc92f687814be3ccb86a64e2` passed GCC, Clang, ASan/UBSan and native AArch64. The current head extends collections with revisioned data-source snapshots and stale-snapshot rejection; it must pass the same four-way M3 UI gate before that extension is treated as validated.

See `docs/M3_2_SEMANTIC_UI_FOUNDATION.md`, `docs/M3_2_ENML_VISUAL_LANGUAGE.md`, `docs/REFERENCE_ANDROID_UI_DESIGN.md`, and `docs/REFERENCE_NOTES_2026_08_09_UI.md`.

Remaining M3.2 work:

- actual renderer-owned shaping/font-provider integration using platform-owned font assets; the bounded shaper seam exists, but ENML does not yet ship a production shaper
- line breaking, bidi paragraph resolution and real measurement/reflow integration above the validated shaping contract
- collection item-content publication and mutation/change notification above the revisioned snapshot/key contract
- bounded opaque-first 2D material rasterization before live translucency/blur
- public app-facing semantic UI API/OSIDL after the in-process contracts stabilize
- platform accessibility service/bridge above semantic snapshots
- deterministic input/focus routing integration without exposing `/dev/input` or compositor internals to apps
- compositor-deadline-aware animation scheduling for the existing motion roles
- secure-system design attribution that application surfaces cannot request or counterfeit
- Figma design-system alignment using the same semantic token vocabulary
- usability/accessibility evaluation before freezing the visual language

Hardware DRM/KMS/GPU backend work, production shader implementation, rich shell visual identity, telephony UI, verified boot and production hardware key providers remain later slices.
