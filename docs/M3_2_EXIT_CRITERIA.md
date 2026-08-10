# M3.2 — merge exit criteria

This checklist defines what “good enough to merge M3.2” means. It is intentionally narrower than “the whole phone OS is finished”: M3.2 should leave ENML with a trustworthy, bounded semantic UI/render/input/accessibility foundation that later hardware, telephony, update, power and production graphics milestones can build on without redesigning the core contracts.

**Implementation authority:** references teach principles; ENML determines implementation; external systems are not the design specification.

## Must be true before M3.2 leaves draft

### Semantic UI and accessibility

- semantic tree capacities, depth, UTF-8 validation, roles/state/actions and stale-ID behavior remain explicitly bounded;
- focus, effective visibility and action authorization have one source of truth in `SemanticTree`;
- accessibility meaning comes from semantic projection, not framebuffer scraping/OCR;
- accessibility snapshots are revision-bound and stale action requests fail closed;
- accessibility privilege is bound to a trusted principal and a nonzero runtime `AccessibilitySessionId` so revision/node tuples cannot be replayed against another session;
- cross-process accessibility transport preserves exact application/session ownership and does not require an idle polling loop or unbounded serialized tree;
- the trusted accessibility service is a real Supervisor-managed principal, obtains its App Manager broker only as a private Supervisor-injected capability, and authenticates its own administration sender through kernel credentials plus the generation-local identity registry;
- service-side application-session storage remains fixed-capacity and clears stale/dead peers rather than accumulating a global registry;
- editable-text accessibility is not exposed until the text-input/IME payload contract is defined.

### Collections

- deep collections remain virtualized with fixed materialized capacity independent of logical item count;
- stable keys preserve semantic continuity through insert/remove/reorder;
- snapshots, change sets, recycler bindings and visible content publication are revision-consistent;
- stale, zero/duplicate-key and malformed-content sources fail closed;
- collection transport remains private, session-bound, pull-only and bounded; producers do not accumulate notification queues or own polling/prefetch workers;
- collection capabilities are minted only after the exact application runtime session is authenticated from kernel credentials/trusted broker identity, and the request cannot name another process, consumer principal, session id or descriptor;
- unclaimed collection endpoints are stored only inside the exact live application instance with a fixed per-instance capacity; one-shot consumer claims require the exact app plus runtime-minted session and do not fall through to another collection;
- the eventual public/OSIDL shape is record/message based and does not expose in-process callback pointers.

### Input

- the compositor chooses the target surface from authoritative scene state;
- input targeting is tied to exact surface generation, owner and presented frame;
- stale targets can be revalidated immediately before privileged delivery;
- only surface-local coordinates cross out of the compositor targeting boundary;
- physical-to-logical conversion is bounded and deterministic;
- semantic hit testing matches visible ordering and does not click through disabled/non-actionable top overlays by default;
- no application receives `/dev/input`, evdev structures or compositor-private scene state;
- global-scene hit-test/validation RPC is authenticated with kernel packet credentials plus a supervisor-resolved trusted input principal;
- ordinary application principals cannot use the privileged input operations even when they possess a compositor endpoint;
- an application obtains its private runtime input endpoint only through its already-authenticated App Manager session and supplies no target process/principal/surface/native descriptor in that request;
- target-process delivery preserves the compositor-issued exact owner/surface/frame identity and routes only to a live instance with the same `PeerIdentity`;
- changing the target identity cannot redirect an event to another or merely convenient running application;
- trusted and application receive sides reject replay/non-monotonic event sequences, including across endpoint reacquisition;
- a stalled application cannot block trusted delivery indefinitely or create an unbounded userspace input queue; delivery uses a bounded nonblocking transport and fails closed when unavailable.

### Rendering and visual identity

- the deterministic render-command path remains independent of GPU/vendor handles and concrete font file paths;
- an opaque/economy CPU path can render meaningful ENML geometry, hierarchy, focus/state and visible text without live backdrop effects;
- typography uses renderer-owned font/shaping/glyph seams with validated bounded output;
- production Unicode/font integration is selected and reviewed before claiming production text support;
- fallback family selection does not split one Unicode grapheme into unrelated font runs;
- authored swept/continuous contours retain their identity when premium effects are reduced;
- contour edge quality is acceptable at phone-scale densities without requiring an unbounded general path engine;
- all CPU material/depth/focus/fringe passes share one bounded physical contour interpretation so geometry does not drift between renderer stages;
- depth/lighting remains bounded and cannot erase focus/state cues;
- live translucency/backdrop is enabled only after opaque figure/ground and state remain clear;
- the actual trusted-system visual pass is compositor-owned and cannot be requested or granted authority by application surfaces;
- visual imitation of the mark by an application is never treated as trusted authority; real trust remains bound to compositor-authorized role, ordering, capture and input state;
- trust-attribution state changes have bounded old/new mark damage so a future partial hardware compositor does not need to convert every trusted-mark transition into a full-screen redraw.

### Performance, power and security

- no new permanent worker, timer, scanner or polling loop exists solely to support UI, text, collections, motion, input or accessibility;
- animation scheduling remains compositor-opportunity driven and becomes quiet when no useful work exists;
- all fixed capacities and memory ownership rules remain documented/tested;
- privileged crossings re-authorize identity/state rather than trusting application claims;
- unsupported/malformed/stale inputs fail closed with stable error domains;
- economy/capability/accessibility fallbacks preserve semantic behavior and recognizable ENML identity.

### Validation

- M3 Semantic UI passes GCC, Clang, ASan/UBSan and native AArch64;
- M3 Display/Compositor passes GCC, Clang, ASan/UBSan and native AArch64;
- M1/M2 workflows touched by shared App Manager/runtime-session changes remain green;
- supervised accessibility and authenticated collection-lifecycle integration tests run on the relevant native/sanitizer matrices rather than existing only as host-only unit fixtures;
- the current head, not merely an older baseline, is green before the PR leaves draft;
- focused tests cover stale/replay/ownership/malformed paths for each new security boundary;
- CI concurrency cancels superseded M3 PR runs so development does not create an ever-growing validation backlog.

## Allowed to remain later work

M3.2 does not need to pretend to finish raw hardware input-device discovery, full gesture/multitouch policy, DRM/KMS/GPU hardware backends, production shader stacks, telephony/radio, verified boot/attestation, production TEE/HSM/TPM providers, recovery/update, complete suspend/resume/power management, final app SDK/marketplace policy, editable-text/IME behavior, synthetic generated-overflow semantics, final font assets or every final ENML visual asset.

Those are later milestones. M3.2 is complete when the semantic UI, renderer, text, input, accessibility, collection and trusted-presentation contracts are stable enough that those later layers do not need to bypass or replace them.

## Exit-review evidence

Validated checkpoint `2edcd41961f30a52afc89d3c6e55d3acf194ed24` satisfied the complete current workflow line: M0; M1; M2 Private Storage; M2 Key Service; M2 Service Broker; M3 Semantic UI on GCC/Clang/ASan+UBSan/native AArch64; and M3 Display/Compositor on GCC/Clang/ASan+UBSan/native AArch64.

That checkpoint includes grapheme-safe font fallback, semantic glyph paint clipping, the concrete production-oriented Linux text adapter, compositor-owned trusted-mark rasterization and bounded trusted-mark damage planning. Documentation-only commits after that checkpoint must themselves be green before the PR leaves draft.

The one initial GCC Service Broker teardown failure on that checkpoint passed on an unchanged rerun while Clang, sanitizers and native AArch64 passed the original attempt. No authorization, lifetime or timeout requirement was loosened to obtain the passing result.

## Reference discipline

The supplied project sources remain design evidence and engineering guidance. Passing this checklist does not mean copying a historical/vendor architecture or claiming compliance from reference documents. ENML retains its own ABI, security model and original visual language.
