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
- strong `AccessibilitySessionId` binding so a valid revision/node request cannot be replayed against another runtime session;
- accessibility actions re-authorized through the same live `SemanticTree` invariants;
- no OCR/framebuffer scraping, accessibility polling loop or dedicated accessibility worker.

Editable accessibility text remains deferred until bounded caret/selection/text-input/IME semantics exist. Cross-process accessibility session transport remains to be implemented above this authority seam.

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

### Input routing, authenticated targeting and exact-owner delivery

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
- every privileged compositor request passes kernel-credential RPC validation and then the trusted input-principal check;
- fixed bounded `SurfaceInputHit` wire encoding validates enums, local coordinates and role/trust consistency;
- cross-process compositor integration proves trusted-principal success and ordinary-principal denial using real kernel packet credentials;
- authenticated post-READY application runtime-session operation for acquiring a private input event endpoint, with no app-supplied target/process/surface/native descriptor;
- App Manager provisions a fresh `SOCK_SEQPACKET` pair only after matching packet `SCM_CREDENTIALS` against the exact broker-bound application identity;
- fixed 84-byte `ApplicationInputEventV1` preserving exact target `PeerIdentity`, surface/frame identity, surface size and local pointer data without global coordinates/device identifiers;
- `ApplicationManager::deliver_input_event()` routes only by exact live `PeerIdentity`, not caller-selected `ApplicationInstanceId`;
- trusted and application receive-side monotonic sequence guards reject replay; trusted sequence state survives endpoint reacquisition;
- retained App Manager sender is nonblocking so a stalled application cannot block trusted input/lifecycle work or create an unbounded userspace event backlog;
- application receive stream rejects a structurally valid event addressed to another bootstrap identity;
- brokered App Manager integration proves endpoint acquisition, wrong-owner denial, replay denial and exact event receipt before continuing existing service-restart/reacquisition checks;
- no application-facing `/dev/input`, evdev structure, global scene state or input polling worker.

The remaining input work is later hardware adapter/device ownership plus explicit multitouch/gesture/pointer-capture/keyboard/IME contracts. Those later pieces feed this authority chain; they do not replace it.

See `docs/M3_2_INPUT_ROUTING.md` and `docs/M3_2_INPUT_DELIVERY.md`.

### Motion and power discipline

Implemented: bounded event-driven `MotionTimeline`, no animation worker/polling loop, sampling only on compositor/render timing opportunities, at most one requested future compositor tick, reduced-motion through the same path, and no retry spin when timing is absent/stale.

### Trusted system presentation

Implemented: compositor-derived trust classification, secure-system capture exclusion, bounded compositor-owned `TrustedOverlaySnapshot`, and role/classification consistency validation. Application pixels cannot self-mint trusted-system attribution.

The actual ENML trust mark and private compositor rendering remain intentionally unfrozen; they must be original, system-owned and usability-tested.

### Reference/product contract

`docs/PROJECT_VISION.md`, `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md` and `docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md` remain guardrails. Supplied Symbian, Linux/Tizen, security/hardening and UI references are engineering evidence, not templates. ENML keeps its own ABI/security model and its original classic, crafted, luxurious, colorful, dimensional, curve-authored visual language.

### Validation status

Head `d8994bd99fb28fca035331235964fe48f5374ad4` was fully green on both M3 matrices before the exact-owner application input-delivery tranche.

The later application-delivery code has passed the current M3 Display/Compositor matrix, including app runtime-session/input-event focused gates. A legacy M1 workflow initially failed only because its broad `application_` CTest regex discovered the new M3-only input-event test without building that target; the CTest name has now been scoped out of the M1 regex while retaining the M3 display label/target. The **new exact current head must complete all required workflows successfully** before the PR leaves draft.

M3 PR workflows use concurrency grouping so superseded validation is cancelled rather than accumulating an unbounded queue. Display feature-branch pushes are scoped to `pull_request`; push validation remains on `main`.

### Remaining M3.2 work

- implement authenticated cross-process accessibility session transport above `AccessibilityBridgeAuthority` without introducing an unbounded serialized tree or polling loop;
- integrate/review a production renderer-private Unicode/font provider/shaper/raster backend;
- improve contour quality toward bounded analytic/interior coverage and strengthen depth/lighting without obscuring focus/state;
- connect collection content/change publication to bounded message/OSIDL records;
- connect `TrustedOverlaySnapshot` to private compositor rendering and design/usability-test the actual ENML trust mark;
- continue compositor-deadline-aware scene transitions;
- add later text-input/IME, keyboard traversal and gesture contracts without exposing raw hardware APIs;
- freeze public semantic UI/OSIDL only after these invariants stabilize;
- align the eventual Figma component system with the same semantic token vocabulary and evaluate accessibility/usability before freezing the visual language.

Hardware DRM/KMS/GPU backends, production shader stacks, telephony/radio, verified boot/attestation, production TPM/TEE/HSM providers, recovery/update and full power-management integration remain later tracks and must not be faked inside M3.2.
