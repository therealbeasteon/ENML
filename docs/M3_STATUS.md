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

Status: late implementation / exit review on `m3-2-semantic-ui-foundation`.

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
- bounded `core/osaccessibility` snapshot/action records and private `AccessibilitySessionServer`/`AccessibilitySessionClient` RPC;
- authenticated App Manager accessibility broker control: caller identity is derived from packet `SCM_CREDENTIALS` through trusted runtime identity before target decoding/claim;
- a real Supervisor-managed `system.accessibility` process with the canonical accessibility-service principal;
- generic Supervisor private capability fd 6 used to inject only the App Manager broker channel into each service generation; the capability is not returned by `Supervisor::connect()` or the application `ServiceBroker`;
- separate authenticated accessibility administration principal resolved through the service generation's `IdentityRegistry`;
- fixed 16-session service table with persistent per-app RPC sequencing and dead-peer cleanup;
- supervised integration proving claim → semantic snapshot → revision-bound focus action → real application `SemanticTree` change → release;
- the same integration republishes the exact native administration sender under an ordinary principal and proves the already-open endpoint is still denied;
- no OCR/framebuffer scraping, accessibility scanner, polling loop or dedicated worker.

Editable accessibility text remains deferred until bounded caret/selection/text-input/IME semantics exist. Later speech, braille, switch-control, live-region and secure-screen product policy must build above this semantic/session authority rather than bypass it.

See `docs/M3_2_ACCESSIBILITY_BRIDGE.md` and `docs/M3_2_ACCESSIBILITY_TRANSPORT.md`.

### Collections

Implemented:

- virtualization for up to 1,000,000 logical items with bounded materialization and 64-bit deep extents;
- stable 64-bit `CollectionItemKey`, fixed recycler and stable slot retention across insertion/reordering;
- strong revisions/snapshots, bounded mutation sets and stale/zero/duplicate-key rejection;
- bounded revision/key-consistent content publication with primary/secondary semantic labels plus enabled/selected state;
- versioned `core/oscollection` snapshot/change/content records and private pull-only `CollectionSessionServer`/`CollectionSessionClient`;
- private collection session namespace moved to `0x0000F016`, distinct from the supervised accessibility service namespace;
- authenticated application runtime operation for minting producer collection capabilities with no app-supplied target, consumer principal, session id or descriptor;
- fixed **8 unclaimed collection sessions per live application instance** and a monotonic nonzero App Manager session-id allocator;
- App Manager retains each consumer endpoint only inside the exact live application instance;
- one-shot trusted consumer handoff requires `collection_consumer_principal`, the exact live application `PeerIdentity` and exact runtime-minted session id;
- wrong-principal, wrong-session and replayed-claim paths fail closed;
- full runtime integration proves a real launched application acquires the capability, hosts a real collection session server, and the trusted consumer pulls a real revision/item-count snapshot;
- conservative visible-window invalidation and no million-row eager prefetch, producer mutation queue or idle worker.

The in-process producer callbacks remain implementation detail. The later public semantic/OSIDL surface must preserve the record/message/session semantics rather than exposing function pointers. If the consumer moves to a separate privileged UI-host service, the existing one-shot App Manager claim seam still needs a kernel-credential-authenticated control wrapper rather than a serialized caller-principal field.

See `docs/M3_2_COLLECTION_CONTENT.md` and `docs/M3_2_COLLECTION_TRANSPORT.md`.

### Rendering and ENML visual language

Implemented:

- semantic design-token boundary independent of concrete vendor assets/colors;
- opaque/translucent/crystal/smoked/luminous materials, flush→hero depth, authored rectilinear/soft/continuous/swept/capsule contours and bounded motion roles;
- accessibility/capability/power quality fallbacks preserving hierarchy/state/contour identity;
- immutable renderer snapshots, bounded deltas, deterministic `RenderCommandBuffer` and bounded `RenderDamagePlan`;
- caller-owned opaque CPU raster with semantic palette, material tint, authored per-corner geometry, bounded depth/lighting and final focus/outline treatment;
- one renderer-private `PixelContour` lowering/evaluation path shared by material ownership, interior coverage, depth silhouette, focus boundary and complementary outside fringe;
- normalized Q10 circle→squircle evaluation that keeps fourth-power intermediates bounded at maximum valid logical geometry without floating point or compiler-specific 128-bit arithmetic;
- deterministic fixed 2×2 interior/outside coverage and coverage-aware opaque depth silhouettes without heap allocation, general path engine, shader compiler or background cache;
- raised/floating/hero economy depth combines coverage-aware support shadow, leading highlight and restrained trailing occlusion; inset reverses the edge treatment and does not cast an external positive-offset shadow;
- concrete compositor-owned `rasterize_trusted_marks()` CPU fallback renders the bounded ENML trusted-system signature after client composition from `TrustedOverlaySnapshot` metadata only;
- application frames cannot request the trust-mark pass or select its palette/geometry; appearance is not treated as cryptographic proof because technical authority still comes from compositor role/z-order/capture/input policy;
- `trusted_mark_bounds()` and fixed-capacity `plan_trusted_mark_damage()` provide exact old/new compositor-owned mark footprints for moves/additions/removals/classification changes without forcing a full-screen attribution redraw;
- pure client-frame sequence changes create no attribution damage, while ordinary client damage still causes the final trust pass to be reapplied;
- semantic-only changes can generate zero pixel damage, moved nodes damage old+new bounds, and pathological semantic damage lists fall back explicitly to full redraw.

Opaque figure/ground and state remain the identity baseline. Live backdrop/translucency must enhance ENML, not become the only reason it is recognizable.

See `docs/M3_2_CONTOUR_ANTIALIAS.md`, `docs/M3_2_OPAQUE_RASTER_BASELINE.md`, `docs/M3_2_RENDER_DAMAGE.md` and `docs/M3_2_TRUSTED_PRESENTATION.md`.

### Text/font/paragraph rendering

Implemented:

- platform-owned semantic font roles and bounded fallback chains;
- renderer-private opaque font face/provider contracts;
- bounded shaped-text and paragraph contracts with validated UTF-8 clusters, font/direction runs, lines and extents;
- renderer-private glyph-mask provider with real coverage→RGBA painting;
- renderer-owned ascent/descent/line-gap metrics and direct render-command text paint path;
- composed geometry+text opaque frame raster;
- concrete renderer-private Linux adapter using configured platform faces, FreeType metrics/masks, HarfBuzz shaping and ICU bidi/line/grapheme analysis behind ENML's own bounded interfaces;
- fixed semantic face-role table and fixed scratch capacities tied to `SemanticText`/shaped-run limits rather than an unbounded text service;
- reusable renderer-owned shaping buffer removes per-run HarfBuzz buffer construction without adding shared background work;
- real mixed LTR/RTL and wrapping tests plus complete production-backend `RenderCommandBuffer` → frame-pixel coverage;
- font fallback is selected per complete Unicode grapheme so base characters, combining marks, variation selectors and shaping controls are not fragmented into unrelated fallback runs;
- when no configured face covers an entire grapheme, ENML keeps the grapheme intact and permits an explicit missing-glyph result rather than corrupting one user-perceived character across faces;
- empty text is zero-paint and hard blank lines preserve line-box layout without manufacturing fake glyphs;
- render-command glyph pixels are clipped to the semantic command rectangle in addition to the target, so legitimate font overhang/bearings cannot paint into sibling node regions;
- no font scanner, text worker, polling loop or unbounded glyph cache.

The private Linux adapter is an ENML implementation choice, not the platform design specification. It is serialized renderer-owned state today; any future parallel renderer must justify and bound per-worker state or explicit synchronization rather than silently turning font processing into an always-on concurrent service.

Still not finalized: final ENML font assets/licensing/package policy, synthetic ellipsis/source-cluster semantics, editable-text/IME shaping, final density-specific hinting policy, color-font/emoji policy and GPU glyph atlas/hardware text rendering.

See `docs/M3_2_TEXT_RENDERING.md`.

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
- brokered App Manager integration proves endpoint acquisition, wrong-owner denial, replay denial and exact event receipt before continuing service-restart/reacquisition checks;
- no application-facing `/dev/input`, evdev structure, global scene state or input polling worker.

The remaining input work is later hardware adapter/device ownership plus explicit multitouch/gesture/pointer-capture/keyboard/IME contracts. Those later pieces feed this authority chain; they do not replace it.

See `docs/M3_2_INPUT_ROUTING.md` and `docs/M3_2_INPUT_DELIVERY.md`.

### Motion and power discipline

Implemented: bounded event-driven `MotionTimeline`, no animation worker/polling loop, sampling only on compositor/render timing opportunities, at most one requested future compositor tick, reduced-motion through the same path, and no retry spin when timing is absent/stale.

### Trusted system presentation

Implemented: compositor-derived trust classification, secure-system capture exclusion, bounded compositor-owned `TrustedOverlaySnapshot`, role/classification consistency validation, concrete compositor-owned CPU trusted-mark raster and bounded attribution-damage planning. Application pixels cannot self-mint trusted-system attribution.

The future hardware compositor must preserve the same overlay-after-client ordering and bounded old/new mark footprint contract. A lookalike shape drawn by an app is never itself proof of system authority.

### Reference/product contract

`docs/PROJECT_VISION.md`, `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md`, `docs/REFERENCE_ADDITIONS_2026_08_10.md` and `docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md` are guardrails.

**References teach principles. ENML determines implementation. External systems are not the design specification.** Supplied Symbian, Linux, security/hardening, mobile-network and UI material may teach principles, mechanisms, failure modes and tradeoffs; ENML derives its ABI, service topology, wire formats, visual grammar and implementation from its own mission, threat model, resource/power limits, existing invariants and measured behavior.

### Validation status

Head `2edcd41961f30a52afc89d3c6e55d3acf194ed24` completed the full workflow line successfully:

- M0 CI;
- M1 Package and App Foundation;
- M2 Private Storage;
- M2 Key Service;
- M2 Service Broker, including GCC, Clang, ASan/UBSan and native AArch64;
- M3 Semantic UI, including GCC, Clang, ASan/UBSan and native AArch64;
- M3 Display/Compositor, including GCC, Clang, ASan/UBSan and native AArch64.

That validated checkpoint includes grapheme-safe font fallback, production command-to-pixel text rendering, semantic glyph paint clipping and the bounded trusted-mark footprint/damage planner.

The first M2 Service Broker attempt on that checkpoint had one isolated GCC teardown assertion while Clang, sanitizers and native AArch64 passed. Re-running the unchanged GCC job passed; no authorization/lifecycle timeout or production rule was loosened. The rerun result is recorded rather than silently treating the original transient execution as product behavior.

Documentation commits after that checkpoint still require exact-head validation before PR #26 can leave draft. M3 PR workflows use concurrency grouping so superseded validation is cancelled rather than accumulating an unbounded queue.

### Remaining M3.2 exit review

The remaining work is no longer foundational reconstruction. Before leaving draft, review the exact exit checklist against the current head and close only concrete gaps found by tests/review. Known later-product items must not be pulled into M3.2 merely to make the checklist look larger.

Items intentionally still outside the M3.2 completion claim include:

- final ENML font assets/licensing, synthetic generated-overflow semantics, editable text/IME, color-font policy and GPU glyph atlas;
- hardware DRM/KMS/GPU composition, while preserving the already-defined trusted overlay-after-client and bounded damage contract;
- user-study/usability refinement of the baseline trusted mark;
- raw hardware input discovery plus multitouch/gesture/pointer-capture/keyboard/IME product contracts;
- telephony/radio, verified boot/attestation, production TPM/TEE/HSM providers, recovery/update and complete power-management integration;
- final marketplace/application distribution/product shell work.

Public semantic UI packaging should freeze only the stable ENML semantic contracts. Cross-process collection/accessibility protocols must remain bounded message/session contracts; in-process UI implementation details and private renderer/library objects must not be promoted into public ABI merely to imitate another platform.
