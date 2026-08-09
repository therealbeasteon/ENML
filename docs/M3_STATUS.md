# M3 Status

## M3.0 — display/compositor ownership foundation

Status: complete and merged.

Implemented:

- additive stable `ErrorDomain::display` and no-exceptions/no-RTTI `core/osdisplay`;
- generation-scoped strong `SurfaceId`, bounded display geometry/safe insets/timing;
- application, popup, system-chrome and secure-system roles with exact `PeerIdentity` ownership;
- one application root per live process in the initial phone model and popup attachment constrained to its exact-process parent;
- compositor-owned application stacking; applications cannot choose global z or self-activate;
- trusted shell/secure-UI principals and secure-system top-band/capture exclusion;
- fixed 64-surface global and eight-surface-per-principal budgets;
- bounded frame metadata/damage, replay rejection, geometry invalidation and deterministic scene snapshots;
- trusted top-down hit testing over visible/input-enabled/framed surfaces;
- GCC, Clang, ASan/UBSan and native AArch64 display gates.

See `docs/M3_0_DISPLAY_COMPOSITOR_FOUNDATION.md`.

## M3.1 — typed shared buffers + supervised compositor service

Status: complete and merged in PR #25.

Implemented:

- generation-scoped `BufferId`/`SurfaceId` namespaces derived from trusted Supervisor generation;
- fixed shared-buffer count and byte ceilings, Linux memfd backing/seals and move-only leases;
- exact `PeerIdentity` buffer ownership, process revocation and scene invalidation on buffer release;
- supervised `system.compositor` using the existing pidfd-backed identity registry;
- per-message kernel credential validation before public compositor operations;
- typed surface/buffer/frame operations and restart integration proving stale channels/IDs do not alias the replacement service generation;
- GCC, Clang, ASan/UBSan and native AArch64 display/service gates.

See `docs/M3_1_SHARED_BUFFER_COMPOSITOR_SERVICE.md`.

## M3.2 — bounded semantic UI, text and original ENML visual foundation

Status: implementation in progress on `m3-2-semantic-ui-foundation`.

`docs/M3_2_EXIT_CRITERIA.md` is now the objective definition of when this branch is good enough to leave draft. M3.2 completion means these semantic/render/input/accessibility contracts are stable and current-head validated; it does not pretend to finish later DRM/KMS/GPU, telephony, verified boot, update/recovery or full power-management milestones.

### Semantic UI, layout and accessibility

Implemented:

- additive `ErrorDomain::ui` and no-exceptions/no-RTTI `core/osui`;
- strong monotonic `UiNodeId` plus separate semantic `StyleTokenId`;
- fixed 256-node tree, 32 direct children per node, depth 16 and fixed 160-byte validated UTF-8 labels;
- semantic roles/state/actions with unique focus and effective ancestor visibility authorization;
- fixed-capacity accessibility projection from semantic nodes rather than framebuffer pixels;
- accessibility-hidden grouping/re-parenting and 100–300% text scaling;
- Q6 density-independent geometry, safe-inset-aware responsive list/detail layout and minimum touch-target policy;
- revision-bound `AccessibilityServiceSnapshot` and stale-snapshot action rejection;
- accessibility actions re-authorized through the same `SemanticTree::focus()` / `dispatch_action()` invariants;
- `AccessibilityBridgeAuthority` binding privileged snapshot/action access to a supervisor-assigned trusted accessibility principal;
- editable-text accessibility deliberately deferred until bounded text-input/caret/selection/IME semantics exist;
- no OCR/framebuffer scraping, accessibility polling loop or accessibility worker introduced by this layer.

See `docs/M3_2_ACCESSIBILITY_BRIDGE.md` and `docs/M3_2_SEMANTIC_UI_FOUNDATION.md`.

### Collections

Implemented:

- virtualization planning for up to 1,000,000 logical items with 64-bit deep scroll/content extents;
- fixed recycler bounded by the semantic child budget;
- strong 64-bit stable `CollectionItemKey` independent of mutable index;
- stable slot retention through insertion/reordering;
- strong `CollectionRevision`, captured `CollectionDataSnapshot` and stale-revision rejection;
- zero/duplicate-key and malformed-source rejection;
- bounded ordered `CollectionChangeSet` with at most 16 insert/remove/move/update/reset operations;
- conservative visible-window invalidation so distant changes need not rematerialize visible rows;
- bounded revision/key-consistent `CollectionContentWindow` publication;
- required primary semantic label, optional secondary label and enabled/selected state without renderer/font/texture pointers;
- content callback receives the exact already-validated stable key for its captured revision, preventing identity/content drift;
- publication remains bounded to the materialized window and introduces no million-row prefetch or idle worker.

The callback backend remains an internal seam, not public ABI. The eventual app-facing protocol should transport bounded records/messages rather than implementation-owned function pointers.

See `docs/M3_2_COLLECTION_CONTENT.md`.

### ENML visual and concrete raster foundation

Implemented:

- semantic design-token table independent of concrete vendor colors/assets;
- optical materials: opaque/translucent/crystal/smoked/luminous;
- flush/inset/raised/floating/hero depth roles;
- authored rectilinear/soft/continuous/swept/capsule contours with asymmetric swept geometry;
- micro/responsive/transition/reveal motion roles;
- reduced-transparency, reduced-motion, high-contrast and 100–300% text-scale resolution;
- economy/balanced/full implementation budgets and explicit renderer capability profile;
- capability/power/accessibility fallbacks that preserve hierarchy, state and contour identity;
- immutable renderer snapshots, bounded deltas and deterministic fixed-capacity `RenderCommandBuffer`;
- first concrete caller-owned opaque CPU raster with semantic palette resolution, material tint, per-corner contour geometry, depth fallback, leading-edge light and final focus/outline treatment;
- deterministic fixed 2×2 subpixel outside-fringe antialiasing for continuous/swept silhouettes without floating point, heap allocation, path engine or shader compiler;
- `rasterize_opaque_frame()` and `rasterize_opaque_frame_with_text()` composed CPU/economy frame paths.

The opaque renderer remains the identity baseline. Live translucency/backdrop filtering is not allowed to become the only thing that makes ENML recognizable. Current contour AA is a bounded first stage; true analytic/interior edge coverage remains work before final visual quality is claimed.

See `docs/M3_2_CONTOUR_ANTIALIAS.md`, `docs/M3_2_OPAQUE_RASTER_BASELINE.md` and `docs/M3_2_ENML_VISUAL_LANGUAGE.md`.

### Text/font/paragraph rendering

Implemented:

- platform-owned semantic font-family roles and bounded fallback chains;
- renderer-private `FontProviderBackend` mapping roles to opaque `FontFaceId` values;
- validated face sets with no application font paths, vendor family names or native handles;
- bounded `ShapedText` contract: at most 160 glyphs, 32 font/direction runs and 16 lines;
- UTF-8 cluster-boundary, fallback-family, direction, line partition and logical-extent validation;
- renderer-private shaping and paragraph seams with explicit width/line/wrap/overflow/base-direction constraints;
- deliberate refusal to implement a partial home-grown bidi/script shaper in `osui`;
- renderer-private bounded glyph-mask provider (up to 512 × 512 per glyph) and real coverage-to-RGBA painting;
- renderer-owned font vertical metrics and baseline placement from validated ascent/descent rather than guessed percentages;
- fallback-aware line-box validation;
- direct `RenderContentKind::text` → face resolution → paragraph shaping → line metrics → glyph-mask paint path;
- no text worker, font scanner, polling loop or unbounded glyph cache.

ENML does **not** yet claim a production Unicode/font backend, final font assets, final hinting, color-font support or GPU glyph atlas. A reviewed production renderer-private implementation still has to plug into these seams before production text support is claimed.

See `docs/M3_2_TEXT_RENDERING.md`.

### Input routing and trusted delivery authority

Implemented:

- semantic `LogicalPoint` routing for activate/focus/toggle/select;
- immutable semantic-snapshot validation before trusting IDs/parent chains/depth/bounds/actions;
- deepest visible hit ownership with equal-depth overlap matching the current renderer's later-painted order;
- ancestor-path action resolution so text inside a button routes naturally to the button;
- visible disabled/non-actionable top overlays block unrelated lower-sibling click-through by default;
- live dispatch is re-authorized through `SemanticTree`;
- compositor-owned `SurfaceInputHit` chooses the topmost visible/framed/input-enabled surface from authoritative scene state;
- input hits carry exact generation-scoped surface ID, exact owner, role, surface size, presented frame sequence, trusted classification and **surface-local** coordinates;
- buffer invalidation, visibility change, bounds change or new frame can invalidate a previously issued hit;
- `validate_input_hit()` re-checks owner/role/frame/size/visibility/input eligibility immediately before privileged delivery, providing an explicit hit-test→delivery TOCTOU defense;
- `InputBridgeAuthority` allows authoritative global-scene targeting only to a supervisor-assigned trusted input principal;
- `logical_point_from_surface_pixel()` maps authorized surface-local physical pixels into semantic Q6 space using bounded integer arithmetic, non-integer scale support and half-open edge rejection;
- no application-facing `/dev/input`, evdev structure, raw global scene state or input polling thread.

The authenticated cross-process transport around this authority seam is still pending. It must use the existing kernel-credential/identity-registry pattern and must not accept an application-supplied target surface as authority.

See `docs/M3_2_INPUT_ROUTING.md`.

### Motion and power discipline

Implemented:

- bounded event-driven `MotionTimeline`;
- no animation worker/timer thread, polling loop or unbounded animation queue;
- animation sampled only on compositor/render timing opportunities;
- at most one caller-supplied future compositor tick requested;
- reduced-motion uses the same bounded path;
- absent/stale timing opportunities cause no retry spin.

The phone should become quiet when there is no useful visual work.

### Trusted system presentation

Implemented:

- compositor-derived `TrustedPresentation` classification;
- apps/popups cannot self-mint system or secure-system trust attribution;
- secure-system capture exclusion remains separate from visual attribution;
- bounded compositor-owned `TrustedOverlaySnapshot` containing trusted authority/bounds/frame sequence but no app-selected icon, color, string, texture or shader;
- role/classification consistency checks reject inconsistent records.

The actual ENML trust mark and its private compositor/backend rendering are intentionally not frozen yet; it must be original, system-owned and usability-tested rather than copied from another platform.

See `docs/M3_2_TRUSTED_PRESENTATION.md`.

### Reference/product contract

`docs/PROJECT_VISION.md`, `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md` and `docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md` remain the project guardrails.

The supplied Symbian, Linux/Tizen, hardening/security, mobile-network, cryptography and UI/UX material is engineering evidence and guidance, not a template. ENML keeps its own ABI, ownership model and visual language. The visual target remains original, classic/crafted/luxurious, colorful, dimensional and curve-authored, while remaining fast, legible and recognizably ENML when premium effects are reduced.

### Validation status

Head `daf508e1f0612db9d1af1c19b025f6548fbccc88` completed the M3 Semantic UI matrix on GCC, Clang, ASan/UBSan and native AArch64, and its M1, M2 key/private-storage/service-broker and M3 Display/Compositor runs completed successfully.

The current branch extends that validated baseline with compositor-authoritative input localization/stale-hit revalidation, trusted input/accessibility principal authority seams, physical→logical input normalization, revision-bound accessibility actions, bounded collection content publication and CI concurrency cleanup. The **current head must pass** fresh GCC, Clang, ASan/UBSan and native-AArch64 M3 UI and M3 Display gates before this tranche is considered validated. Queued/pending CI is not a pass.

M3 UI/Display PR workflows now group concurrency by PR/ref rather than head SHA so future superseded M3 validation runs cancel instead of growing an unbounded queue.

### Remaining M3.2 work

- complete authenticated cross-process transport around the trusted input/accessibility authority seams;
- integrate and review the production renderer-private Unicode/font provider/shaper/raster backend;
- improve contour quality from outside-fringe AA toward bounded analytic/interior coverage;
- strengthen bounded depth/lighting without allowing optical effects to erase focus/state;
- connect collection publication to bounded message/OSIDL records without exposing callback pointers;
- connect `TrustedOverlaySnapshot` to private compositor rendering and design/usability-test the actual ENML trust mark;
- continue compositor-deadline-aware scene transitions;
- add later text-input/IME, keyboard focus traversal and gesture contracts without smuggling raw hardware APIs into application UI;
- freeze public semantic UI/OSIDL only after these in-process invariants stabilize;
- align the eventual Figma component system with the same semantic token vocabulary and evaluate accessibility/usability before freezing the visual language.

Hardware DRM/KMS/GPU backends, production shader implementation, telephony/radio, verified boot/attestation, production TPM/TEE/HSM providers, recovery/update and full power-management integration remain later tracks and must not be faked inside M3.2.
