# M3.2 — merge exit criteria

This checklist defines what “good enough to merge M3.2” means. It is intentionally narrower than “the whole phone OS is finished”: M3.2 should leave ENML with a trustworthy, bounded semantic UI/render/input/accessibility foundation that later hardware, telephony, update, power and production graphics milestones can build on without redesigning the core contracts.

## Must be true before M3.2 leaves draft

### Semantic UI and accessibility

- semantic tree capacities, depth, UTF-8 validation, roles/state/actions and stale-ID behavior remain explicitly bounded;
- focus, effective visibility and action authorization have one source of truth in `SemanticTree`;
- accessibility meaning comes from semantic projection, not framebuffer scraping/OCR;
- accessibility snapshots are revision-bound and stale action requests fail closed;
- platform accessibility transport design preserves application/session ownership and does not require an idle polling loop;
- editable-text accessibility is not exposed until the text-input/IME payload contract is defined.

### Collections

- deep collections remain virtualized with fixed materialized capacity independent of logical item count;
- stable keys preserve semantic continuity through insert/remove/reorder;
- snapshots, change sets, recycler bindings and visible content publication are revision-consistent;
- stale, zero/duplicate-key and malformed-content sources fail closed;
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
- the remaining target-process event transport cannot be redirected by an application-supplied target ID and must preserve the compositor-issued owner/surface/frame identity through delivery.

### Rendering and visual identity

- the deterministic render-command path remains independent of GPU/vendor handles and concrete font file paths;
- an opaque/economy CPU path can render meaningful ENML geometry, hierarchy, focus/state and visible text without live backdrop effects;
- typography uses renderer-owned font/shaping/glyph seams with validated bounded output;
- production Unicode/font integration is selected and reviewed before claiming production text support;
- authored swept/continuous contours retain their identity when premium effects are reduced;
- contour edge quality is acceptable at phone-scale densities without requiring an unbounded general path engine;
- depth/lighting remains bounded and cannot erase focus/state cues;
- live translucency/backdrop is enabled only after opaque figure/ground and state remain clear;
- the actual trusted-system visual mark is compositor-owned and cannot be requested or counterfeited by application surfaces.

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
- the current head, not merely an older baseline, is green before the PR leaves draft;
- focused tests cover stale/replay/ownership/malformed paths for each new security boundary;
- CI concurrency cancels superseded M3 PR runs so development does not create an ever-growing validation backlog.

## Allowed to remain later work

M3.2 does not need to pretend to finish DRM/KMS/GPU hardware backends, production shader stacks, telephony/radio, verified boot/attestation, production TEE/HSM/TPM providers, recovery/update, complete suspend/resume/power management, final app SDK/marketplace policy or every final ENML visual asset.

Those are later milestones. M3.2 is complete when the semantic UI, renderer, text, input, accessibility, collection and trusted-presentation contracts are stable enough that those later layers do not need to bypass or replace them.

## Reference discipline

The supplied project sources remain design evidence and engineering guidance. Passing this checklist does not mean copying a historical/vendor architecture or claiming compliance from reference documents. ENML retains its own ABI, security model and original visual language.
