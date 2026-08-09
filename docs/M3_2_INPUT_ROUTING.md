# M3.2 — bounded semantic input routing

This slice establishes the first deterministic physical-surface-to-semantic-action path for ENML UI while preserving compositor authority and keeping raw hardware input private.

## Purpose

Applications should reason about semantic actions such as activate, focus, toggle and select. They should not receive Linux input device descriptors, evdev structures, compositor-private scene ordering or global hardware coordinates.

The input path is deliberately split across subsystem boundaries:

`trusted hardware/input adapter`
→ authenticated compositor RPC
→ global display point inside the compositor
→ compositor-authorized `SurfaceInputHit`
→ surface-local pixel point
→ bounded logical viewport transform
→ semantic `LogicalPoint`
→ semantic hit routing/action authorization

This keeps display ownership, coordinate normalization and semantic action authority separate instead of building one privileged catch-all input/UI service.

## Authenticated compositor transport

The supervised compositor now exposes two narrowly scoped privileged operations on its existing authenticated RPC endpoint:

- `compositor_op_input_hit_test` asks the compositor to choose the target for one global display-space point;
- `compositor_op_input_validate` revalidates the returned owner/surface/frame tuple immediately before downstream delivery.

`InputCompositorClient` is a separate privileged facade rather than an extension of the ordinary application `CompositorClient`. The C++ type is not itself authority. Each request still traverses `validate_rpc_request()`, which resolves kernel-supplied per-message credentials through trusted runtime identity, and then `InputBridgeAuthority` requires the configured input-service principal.

An ordinary application principal can possess a brokered compositor endpoint and still receives `input_authority_denied` for the scene-query operations. Knowledge of the input-service principal value is not sufficient to mint that identity; the runtime identity resolver supplies the caller principal from supervisor-published process state.

The wire representation of `SurfaceInputHit` is fixed and bounded. It carries generation-scoped surface identity, exact owner, role, surface size, frame sequence, local point and compositor-derived trusted-presentation classification. Decoding rejects malformed enum values, invalid local coordinates and role/trust-classification inconsistencies before the hit is re-authorized against live compositor state.

The integration gate uses a real `SOCK_SEQPACKET` channel across `fork()`, so request authorization observes kernel packet credentials rather than a caller-provided PID field. It proves both the trusted-principal success path and ordinary-principal denial path. The transport remains synchronous/event-driven and creates no input worker or polling loop.

## Compositor-owned surface targeting

`osdisplay::Compositor::hit_test_input()` is the privileged display-side seam. It chooses the topmost visible, framed and input-enabled surface using the compositor's authoritative scene ordering.

The result contains only the data a trusted delivery bridge needs:

- exact generation-scoped `SurfaceId`;
- exact `PeerIdentity` owner;
- surface role;
- surface pixel size;
- the exact presented frame sequence;
- surface-local x/y coordinates;
- compositor-derived trusted-presentation classification.

The result intentionally does not need to be forwarded wholesale to applications. A platform input service can use owner/surface/frame identity for authorization and deliver only the permitted surface-local semantic event to the target process.

Tying the hit to `frame_sequence` lets delivery policy reason about the exact UI frame the compositor considered visible when the event was targeted. Buffer invalidation removes `has_frame`, so a surface whose presented pixels were revoked cannot continue receiving hits for an image the compositor no longer presents.

`validate_input_hit()` closes the hit-test-to-delivery TOCTOU window without holding a permanent compositor lock: a hidden/moved surface, changed frame, revoked buffer, changed owner/role/size or otherwise stale presentation is rejected before delivery.

## Physical-to-logical normalization

`osui::logical_point_from_surface_pixel()` converts a compositor-authorized surface-local pixel into the logical Q6 viewport used by `SemanticTree`.

The transform:

- uses bounded integer arithmetic only;
- supports non-integer physical/logical scale ratios;
- is half-open on the surface edge rather than clamping out-of-surface coordinates into a control;
- carries only surface dimensions and logical viewport dimensions;
- does not expose global screen coordinates, Linux input-device identity or compositor stacking state to semantic UI.

## Semantic routing contract

`route_pointer_action()`:

- accepts only bounded logical Q6 coordinates;
- accepts pointer-originated semantic actions (`activate`, `focus`, `toggle`, `select`); `set_text` is deliberately not a pointer-routing operation;
- validates the supplied semantic snapshot before trusting IDs, parent chains, roles, bounds, depth or action masks;
- ignores nodes hidden by their own state or by a hidden ancestor;
- chooses the deepest visible semantic node containing the point;
- resolves equal-depth overlap by larger monotonic `UiNodeId`, matching the current renderer's later-painted ordering;
- walks only that winning node's ancestor path to find an enabled node advertising the requested semantic action;
- requires the point to remain inside the actionable ancestor's bounds;
- returns an explicit no-target error when the winning visual/semantic path does not authorize the requested action.

This means labels nested inside buttons naturally route to the button without duplicating action regions on text children.

## No click-through by default

A visible topmost overlay that does not authorize an action blocks unrelated lower siblings. A disabled topmost control also blocks click-through.

This is intentional. Until ENML defines an explicit, reviewed pointer-transparency semantic, the safer default is that what visually/semantically occupies the top hit path owns that region. A decorative or disabled overlay must not accidentally expose a sensitive control underneath it.

Hidden overlays do not participate in the effective hit stack.

## Live tree dispatch

`dispatch_pointer_action()` obtains a fresh snapshot, routes through the deterministic hit-test contract, and then re-authorizes the result through `SemanticTree`.

- focus uses the tree's existing `focus()` invariant checks and updates semantic focus;
- other pointer actions use `dispatch_action()` and therefore retain role/action/enabled/visibility validation;
- routing does not invent application callbacks, gesture threads or raw device handles.

## Resource and power discipline

The current path:

- allocates no heap memory in compositor hit localization or semantic routing;
- creates no worker or timer thread;
- performs no input polling inside `osdisplay`/`osui`;
- owns no background gesture recognizer;
- operates over the existing bounded 64-surface compositor scene and 256-node semantic snapshot;
- uses fixed-size RPC payloads for privileged target/validation requests;
- performs input work only when the platform has an input event to route.

## Security boundary

The authenticated compositor transport now protects the global-scene targeting seam, but this is still not the complete hardware input service.

The remaining platform input service must keep `/dev/input`, seat/device state and calibration private, obtain its runtime principal from trusted supervisor lifecycle state, preserve surface generation/owner/frame identity through delivery, and ensure an event cannot be redirected to another process by application-supplied target IDs.

The compositor does not accept a caller-supplied target `SurfaceId` for hit testing. It chooses the target from authoritative scene state and revalidates the exact result before delivery.

## Visual/UX relationship

The routing rules intentionally track the same scene/semantic ordering used by the compositor and renderer. This reduces the risk that the user sees one surface/control as topmost while input is delivered to a different lower target.

The mechanism does not prescribe ENML's visual appearance. Authored curves, materials, motion and typography remain renderer concerns; input consumes authoritative geometry and semantic action authority rather than copying another platform's gesture model.

## Not yet claimed

This slice does not yet provide:

- raw hardware device discovery/calibration or `/dev/input` ownership;
- multitouch/gesture recognition;
- pointer capture;
- drag/drop;
- scrolling physics;
- keyboard/navigation focus traversal;
- IME/text-edit input;
- hardware key mapping;
- final target-process event transport from the trusted input service into the owning application runtime.

Those must be added as bounded, explicit platform contracts rather than smuggled into the semantic UI layer.
