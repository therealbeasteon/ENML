# M3 Status

## M3.0 — display/compositor ownership foundation

Status: complete and merged.

Implemented: bounded `osdisplay`, generation-scoped `SurfaceId`, exact `PeerIdentity` ownership, application/popup/system-chrome/secure-system roles, compositor-owned stacking, trusted shell/secure UI principals, bounded frame/damage metadata, secure-system capture exclusion, deterministic scene snapshots, authoritative top-down input hit testing and GCC/Clang/ASan+UBSan/native-AArch64 gates.

See `docs/M3_0_DISPLAY_COMPOSITOR_FOUNDATION.md`.

## M3.1 — typed shared buffers + supervised compositor service

Status: complete and merged in PR #25.

Implemented: generation-scoped `BufferId`/`SurfaceId`, bounded shared-buffer counts and bytes, Linux memfd backing/seals, exact buffer ownership/revocation, supervised `system.compositor`, pidfd-backed runtime identity, per-message kernel credential validation, typed surface/buffer/frame RPC and restart integration proving stale channels/IDs cannot alias a replacement compositor generation.

See `docs/M3_1_SHARED_BUFFER_COMPOSITOR_SERVICE.md`.

## M3.2 — bounded semantic UI, text and original ENML visual foundation

Status: implementation in progress on `m3-2-semantic-ui-foundation`.

`docs/M3_2_EXIT_CRITERIA.md` defines when this branch is actually good enough to leave draft. M3.2 is a trustworthy semantic UI/render/input/accessibility foundation, not a claim that later DRM/KMS/GPU, telephony, update/recovery, verified-boot or full power-management tracks are finished.

### Semantic UI, layout and accessibility

Implemented:

- no-exceptions/no-RTTI `core/osui` and additive `ErrorDomain::ui`;
- bounded 256-node semantic tree, 32 children/node, depth 16, strong monotonic `UiNodeId` and fixed validated UTF-8 labels;
- semantic roles/state/actions, unique focus and effective ancestor-visibility authorization;
- fixed accessibility projection from semantics rather than framebuffer pixels;
- Q6 density-independent geometry, safe-inset responsive layout, 100–300% text scale and minimum touch targets;
- revision-bound accessibility snapshots/actions with stale-request rejection;
- `AccessibilityBridgeAuthority` restricting privileged access to a trusted accessibility principal;
- accessibility actions re-authorized through the same live `SemanticTree` invariants;
- no OCR/framebuffer scraping, accessibility polling loop or dedicated accessibility worker.

Editable accessibility text remains deferred until bounded caret/selection/text-input/IME semantics exist.

### Collections

Implemented:

- virtualization for up to 1,000,000 logical items with bounded materialization and 64-bit deep extents;
- stable 64-bit `CollectionItemKey`, fixed recycler and stable slot retention across insertion/reordering;
- strong revisions/snapshots, bounded mutation sets and stale/zero/duplicate-key rejection;
- bounded revision/key-consistent content publication with primary/secondary semantic labels plus enabled/selected state;
- conservative visible-window invalidation and no million-row eager prefetch/idle worker.

The callback backend remains internal. Public app transport must be bounded record/message based rather than exposing implementation-owned function pointers.

### Rendering and ENML visual language

Implemented:

- semantic design-token boundary independent of concrete vendor assets/colors;
- opaque/translucent/crystal/smoked/luminous materials, flush→hero depth, authored rectilinear/soft/continuous/swept/capsule contours and bounded motion roles;
- accessibility/capability/power quality fallbacks preserving hierarchy/state/contour identity;
- immutable renderer snapshots, bounded deltas, deterministic `RenderCommandBuffer` and bounded `RenderDamagePlan`;
- caller-owned opaque CPU raster with semantic palette, material tint, authored per-corner geometry, bounded depth/lighting and final focus/outline treatment;
- deterministic fixed subpixel contour antialiasing without floating point, heap allocation, general path engine, shader compiler or background cache;
- semantic-only changes can generate zero pixel damage, moved nodes damage old+new bounds, and pathological damage lists fall back explicitly to full redraw.

Opaque figure/ground and state remain the identity baseline. Live backdrop/translucency must enhance ENML, not become the only reason it is recognizable.

### Text/font/paragraph rendering

Implemented:

- platform-owned semantic font roles and bounded fallback chains;
- renderer-private opaque font face/provider contracts;
- bounded shaped-text and paragraph contracts with validated UTF-8 clusters, font/direction runs, lines and extents;
- renderer-private glyph-mask provider with real coverage→RGBA painting;
- renderer-owned ascent/descent/line-gap metrics and direct render-command text paint path;
- composed geometry+text opaque frame raster;
- no font scanner, text worker, polling loop or unbounded glyph cache.

ENML still does **not** claim a production Unicode/font backend, final font assets/hinting, color-font support or GPU glyph atlas. A reviewed renderer-private production implementation must plug into the existing seams before production text support is claimed.

### Input routing and authenticated targeting transport

Implemented:

- deterministic semantic Q6 pointer routing for activate/focus/toggle/select with fresh live-tree re-authorization;
- topmost-path ownership, ancestor action resolution and no disabled/non-actionable click-through by default;
- compositor-owned `SurfaceInputHit` containing exact generation-scoped surface, owner, role, surface size, presented frame sequence, trusted classification and **surface-local** coordinates;
- buffer invalidation, visibility/bounds changes and newer frames invalidate stale input hits;
- `validate_input_hit()` re-checks owner/role/frame/size/visibility/input eligibility immediately before delivery;
- `InputBridgeAuthority` binds global-scene targeting to a trusted input principal;
- bounded integer surface-pixel→semantic-Q6 normalization with non-integer scale support and half-open edge rejection;
- authenticated compositor RPC operations for hit-test and pre-delivery validation;
- separate `InputCompositorClient` privileged facade rather than adding global-scene operations to the ordinary app client;
- every privileged request passes normal kernel-credential RPC validation and then the trusted input-principal check;
- fixed bounded `SurfaceInputHit` wire encoding validates enums, local coordinates and role/trust consistency;
- cross-process `SOCK_SEQPACKET`/`fork()` integration proves trusted-principal success and ordinary-principal denial using real kernel packet credentials;
- no application-facing `/dev/input`, evdev structure, global scene state or input polling worker.

The remaining input work is the final trusted input-service→owning-app event transport plus later gesture/multitouch/keyboard/IME contracts. Applications must never choose the target surface as authority.

See `docs/M3_2_INPUT_ROUTING.md`.

### Motion and power discipline

Implemented: bounded event-driven `MotionTimeline`, no animation worker/polling loop, sampling only on compositor/render timing opportunities, at most one requested future compositor tick, reduced-motion through the same path, and no retry spin when timing is absent/stale.

### Trusted system presentation

Implemented: compositor-derived trust classification, secure-system capture exclusion, bounded compositor-owned `TrustedOverlaySnapshot`, and role/classification consistency validation. Application pixels cannot self-mint trusted-system attribution.

The actual ENML trust mark and private compositor rendering remain intentionally unfrozen; they must be original, system-owned and usability-tested.

### Reference/product contract

`docs/PROJECT_VISION.md`, `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md` and `docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md` remain guardrails. Supplied Symbian, Linux/Tizen, security/hardening and UI references are engineering evidence, not templates. ENML keeps its own ABI/security model and its original classic, crafted, luxurious, colorful, dimensional, curve-authored visual language.

### Validation status

Code head `62c42d6e8a60ad34de929258d04352bfc31a8301` completed the M3 Display/Compositor matrix successfully on GCC, Clang, ASan/UBSan and native AArch64, including the authenticated input transport integration gate. Its M3 Semantic UI gates were also green where completed before the subsequent documentation/CI-scoping commits.

The current head extends that validated code only with input-transport documentation, exit-criteria/status synchronization and CI scoping so branch pushes do not duplicate PR display validation. The **current head still must be green** on the required M3 matrices before the PR leaves draft; an older successful code head is not sufficient for merge readiness.

M3 PR workflows use concurrency grouping so superseded validation is cancelled rather than accumulating an unbounded queue. Display branch pushes are now scoped to `main`; feature branches validate through the pull-request event instead of running duplicate push+PR display matrices.

### Remaining M3.2 work

- finish authenticated accessibility transport/session ownership and final trusted input-service→application event delivery;
- integrate/review a production renderer-private Unicode/font provider/shaper/raster backend;
- improve contour quality toward bounded analytic/interior coverage and strengthen depth/lighting without obscuring focus/state;
- connect collection content/change publication to bounded message/OSIDL records;
- connect `TrustedOverlaySnapshot` to private compositor rendering and design/usability-test the actual ENML trust mark;
- continue compositor-deadline-aware scene transitions;
- add later text-input/IME, keyboard traversal and gesture contracts without exposing raw hardware APIs;
- freeze public semantic UI/OSIDL only after these invariants stabilize;
- align the eventual Figma component system with the same semantic token vocabulary and evaluate accessibility/usability before freezing the visual language.

Hardware DRM/KMS/GPU backends, production shader stacks, telephony/radio, verified boot/attestation, production TPM/TEE/HSM providers, recovery/update and full power-management integration remain later tracks and must not be faked inside M3.2.
