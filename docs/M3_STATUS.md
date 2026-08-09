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
- immutable renderer snapshot with revision tracking
- bounded renderer dirty/removal delta and full-resync fallback on bookkeeping overflow
- semantic design token table independent of concrete vendor colors/assets
- typography/spacing/color/simple-shape roles
- optical material roles for opaque/translucent/crystal/smoked/luminous treatment
- separate depth roles for flush/inset/raised/floating/hero hierarchy
- curve roles that can evolve beyond generic rounded rectangles, including continuous and swept contours
- motion roles for micro/responsive/transition/reveal behavior
- multiple semantic accent/tint roles without fixed RGB ABI
- reduced-transparency, reduced-motion and high-contrast visual preference resolution
- text scaling from 100% through 300% and minimum logical touch-target policy
- large-text tests proving row reflow and reduced visible collection window size
- phone/tablet recomposition tests preserving semantic node identity
- adversarial unit tests for malformed UTF-8, oversized/forged labels, stale node IDs, depth/child bounds, role/state/action misuse, focus transfer and accessibility projection
- responsive layout tests for phone/tablet-like viewports, safe insets, policy changes and invalid viewport/policy inputs
- collection-window tests for overscroll, deep scrolling, window limits and out-of-window access
- renderer snapshot/delta tests
- design/reflow tests including optical material and accessibility fallbacks
- dedicated GCC, Clang, ASan/UBSan and native AArch64 semantic-UI gates; the expanded design/reflow branch passed all four gates

See `docs/M3_2_SEMANTIC_UI_FOUNDATION.md`, `docs/M3_2_ENML_VISUAL_LANGUAGE.md`, `docs/REFERENCE_ANDROID_UI_DESIGN.md`, and `docs/REFERENCE_NOTES_2026_08_09_UI.md`.

Remaining M3.2 work:

- renderer-facing deterministic command/geometry representation for resolved style roles;
- deterministic contour/path representation for ENML curve families without binding public ABI to a vendor graphics API;
- text shaping/font fallback and measurement/reflow integration;
- collection data-source/recycling protocol rather than only window planning;
- public app-facing semantic UI API/OSIDL after the in-process contracts stabilize;
- platform accessibility service/bridge above semantic snapshots;
- deterministic input/focus routing integration without exposing `/dev/input` or compositor internals to apps;
- renderer capability/power-quality fallback policy for blur, translucency, depth and motion;
- secure-system design attribution that application surfaces cannot request or counterfeit;
- usability/accessibility evaluation and Figma design-system alignment before freezing the visual language.

Hardware DRM/KMS/GPU backend work, production shader implementation, rich shell visual identity, telephony UI, verified boot and production hardware key providers remain later slices.
