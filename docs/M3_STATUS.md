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

## M3.2 — bounded semantic UI, text and original ENML visual foundation

Status: implementation in progress on `m3-2-semantic-ui-foundation`.

### Semantic/accessibility/layout foundation

Implemented:

- additive `ErrorDomain::ui` and no-exceptions/no-RTTI `core/osui`
- strong `UiNodeId` and separate `StyleTokenId`
- fixed 256-node semantic tree, 32 direct children per node and depth limit 16
- fixed 160-byte validated UTF-8 semantic labels
- monotonic node IDs with no stale-ID reuse inside a live tree
- semantic roles/state/actions with role validation and unique focus
- effective ancestor visibility checks for focus/actions/accessibility
- fixed-capacity accessibility projection from semantic nodes rather than pixels
- decorative accessibility-hidden grouping with descendant re-parenting
- fixed-point density-independent geometry (Q6: 64 units per logical dp)
- safe-inset-aware responsive single-pane/dual-pane list-detail layout
- explicit responsive policy rather than device-class probing
- 100–300% text scaling and minimum logical touch-target policy
- phone/tablet recomposition and large-text reflow tests preserving semantic identity

### Collection virtualization and mutation

Implemented:

- bounded collection virtualization planning for up to 1,000,000 logical items
- deep-list 64-bit scroll/content extent with viewport-relative materialized coordinates
- deterministic fixed-pool recycler bounded by the semantic child budget
- strong 64-bit `CollectionItemKey` independent of mutable index
- stable-key slot retention across insertion/reordering
- strong `CollectionRevision` and bounded `CollectionDataSnapshot`
- revision-scoped source snapshot/key lookup
- stale-revision, zero-key, duplicate-key and malformed-source rejection
- bounded `CollectionChangeSet` with at most 16 ordered insert/remove/move/update/reset operations
- sequential change validation against old/new item counts
- revision-scoped change-source seam
- conservative `collection_change_affects_window()` invalidation so distant updates/appends can avoid needless visible-window work while structural edits before the visible end still trigger rematerialization

The collection app-facing/OSIDL protocol is not frozen yet; these remain internal semantics until the invariants settle.

### ENML visual/render foundation

Implemented:

- semantic design token table independent of concrete vendor colors/assets
- optical material roles: opaque/translucent/crystal/smoked/luminous
- flush/inset/raised/floating/hero depth roles
- authored rectilinear/soft/continuous/swept/capsule contour roles
- bounded contour resolution independent of vendor graphics/path APIs
- micro/responsive/transition/reveal motion roles
- multiple semantic accent/tint roles without fixed RGB ABI
- reduced-transparency, reduced-motion and high-contrast preference resolution
- economy/balanced/full visual quality tiers
- explicit renderer capability profile for alpha compositing, live backdrop, spatial motion and bounded blur
- capability fallback preserving ENML hierarchy/contour identity when richer effects are unavailable
- immutable renderer snapshots with bounded dirty/removal deltas and full-resync fallback
- deterministic fixed-capacity `RenderCommandBuffer`
- explicit separation between accessibility labels and visible renderer text
- first concrete opaque-first CPU raster stage using caller-owned bounded memory
- renderer-owned `RasterTheme` mapping semantic roles to RGBA without making RGB app ABI
- deterministic Q6-to-pixel scaling, clipping and target validation
- actual material tint painting
- per-corner contour rasterization where smoothing changes pixel coverage
- directional opaque depth fallback and leading-edge lighting
- focus/outline treatment applied after optical lighting so interaction state remains legible

The current opaque-first raster is intentionally the identity baseline. Live translucency/backdrop filtering must enhance an already recognizable ENML interface rather than become the only source of identity.

### Text/font/paragraph rendering

Implemented:

- platform-owned semantic font-family roles and bounded fallback chains
- renderer-owned `FontProviderBackend` mapping semantic roles to opaque `FontFaceId` values and bounded metrics
- validated `FontFaceSet` with no application font path/vendor family/native handle exposure
- bounded shaped-text contract: at most 160 glyph records, 32 direction/font runs and 16 lines per semantic text value
- UTF-8 cluster-boundary, fallback-family, direction, line partition and extent validation
- `TextShaperBackend` and production-oriented `FontAwareTextShaperBackend`
- measurement derived from validated glyph advances rather than byte/code-point estimates
- bounded paragraph contract with explicit maximum width, maximum lines, wrap mode, overflow mode and base-direction intent
- renderer-owned `FontAwareParagraphShaperBackend` for production Unicode bidi/script shaping/line breaking
- deliberate refusal to implement a partial home-grown Unicode bidi/shaping algorithm in `osui`
- paragraph output validation against the existing shaped-text contract and width/line budgets
- renderer-private `GlyphMaskProviderBackend`
- transient bounded glyph coverage masks up to 512 × 512 pixels with stride/capacity/bearing validation
- real coverage-to-RGBA glyph rasterization into caller-owned target memory
- empty glyph masks for spacing/non-ink glyphs
- clipped coverage blending with explicit unavailable/failed/malformed provider errors
- no text worker thread, background font scanner, polling loop or unbounded glyph cache introduced by this slice

See `docs/M3_2_TEXT_RENDERING.md`.

ENML still does **not** claim a production Unicode shaper, final platform font assets, final hinting, color-font support or a GPU glyph atlas. The bounded seams and real mask-to-pixel path are now ready for those renderer-private implementations.

### Motion and power discipline

Implemented:

- bounded event-driven `MotionTimeline`
- no animation worker/timer thread, polling loop or unbounded animation queue
- animation is sampled only when the compositor/render loop provides a timing opportunity
- at most one future frame request from a caller-supplied compositor tick
- reduced-motion behavior uses the same bounded path
- stale/absent compositor ticks produce no retry spin

This preserves the original requirement that the phone should become quiet when there is no useful work.

### Trusted system presentation

Implemented:

- compositor-derived `TrustedPresentation` classification
- applications/popups cannot self-mint system/secure-system trust attribution
- secure-system capture exclusion remains independent of visual attribution
- bounded `TrustedOverlaySnapshot` as the first non-buffer compositor-owned trust-attribution render input
- overlay entries include only visible, framed, correctly role-attributed trusted surfaces
- repeated role/classification consistency validation rejects inconsistent application + secure-system records
- overlay carries authority/bounds/frame sequence but no application-selected color/icon/texture/string/shader
- exact ENML trust-mark appearance remains a future compositor/backend decision so secure chrome is not copied from another platform

See `docs/M3_2_TRUSTED_PRESENTATION.md`.

### Reference and product-vision contract

The branch now contains `docs/PROJECT_VISION.md`, `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md`, and `docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md`.

The implementation contract is:

- Symbian guides compactness, modular client/server ownership, asynchronous/event-driven work and hardware isolation; ENML does not copy Symbian ABI or historical implementation choices.
- Hardening references guide least privilege, isolation, attack-surface reduction and secure defaults.
- Linux/Tizen/mobile architecture material guides private hardware adaptation and portability boundaries without defining public ENML ABI.
- Mobile-performance sources reinforce responsiveness, battery discipline and minimal background work.
- UI references guide usability, reachability, accessibility, material/motion principles and failure modes without copying visual identity.
- Historical security material is design evidence, not a current compliance claim or license to use obsolete cryptography.

The original visual direction remains a hard requirement: ENML must be original, classic/crafted/luxurious, colorful, dimensional, curve-authored, capable of translucency and meaningful motion, and still fast/legible/recognizable when premium effects are reduced.

### Validation status

A prior pre-text-raster head passed the M3 Semantic UI matrix on GCC, Clang, ASan/UBSan and native AArch64.

A later CI failure was diagnosed as workflow wiring rather than a code-test failure: `ui_motion_timeline_test` had been registered with CTest but omitted from the explicit workflow build-target list. The workflow now builds every registered M3 UI target, including motion, paragraph layout, glyph raster and collection changes, across all four UI jobs.

The current branch head must complete the fresh GCC, Clang, ASan/UBSan and native-AArch64 UI/display runs before the new paragraph/glyph/collection/trusted-overlay tranche is considered validated. Do not treat queued CI as a pass.

### Remaining M3.2 work

- integrate a reviewed production renderer-private font provider + Unicode shaping implementation with the existing bounded contracts
- connect collection change/content publication to the eventual semantic app API/OSIDL without exposing implementation-owned function pointers
- improve contour edges from the deterministic binary/smoothing baseline to bounded anti-aliased/vector-quality coverage
- connect glyph painting into the full render-command text path
- connect `TrustedOverlaySnapshot` to the future private hardware/display compositor backend and design/usability-test the actual ENML trust mark
- strengthen bounded depth/lighting before adding live translucency/backdrop filtering
- continue compositor-deadline-aware motion integration with real scene transitions
- add platform accessibility service/bridge above semantic snapshots
- add deterministic input/focus routing without exposing `/dev/input` or compositor internals to applications
- freeze public app-facing semantic UI API/OSIDL only after these in-process contracts stabilize
- align the Figma component/design system to the same semantic token vocabulary and evaluate usability/accessibility before freezing visual language

Hardware DRM/KMS/GPU backend work, production shader implementation, telephony/radio integration, verified boot/attestation, production TPM/TEE/HSM providers, recovery/update and full power-management integration remain later tracks. They must not be faked inside M3.2.
