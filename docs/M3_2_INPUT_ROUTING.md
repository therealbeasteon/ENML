# M3.2 — bounded semantic input routing

This slice establishes the first deterministic physical-surface-to-semantic-action path for ENML UI while preserving compositor authority and keeping raw hardware input private.

## Purpose

Applications should reason about semantic actions such as activate, focus, toggle and select. They should not receive Linux input device descriptors, evdev structures, compositor-private scene ordering or global hardware coordinates.

The input path is deliberately split across subsystem boundaries:

`trusted hardware/input adapter`
→ global display point inside the compositor
→ compositor-authorized `SurfaceInputHit`
→ surface-local pixel point
→ bounded logical viewport transform
→ semantic `LogicalPoint`
→ semantic hit routing/action authorization

This keeps display ownership, coordinate normalization and semantic action authority separate instead of building one privileged catch-all input/UI service.

## Compositor-owned surface targeting

`osdisplay::Compositor::hit_test_input()` is the privileged display-side seam. It chooses the topmost visible, framed and input-enabled surface using the compositor's authoritative scene ordering.

The result contains only the data a future trusted delivery bridge needs:

- exact generation-scoped `SurfaceId`;
- exact `PeerIdentity` owner;
- surface role;
- surface pixel size;
- the exact presented frame sequence;
- surface-local x/y coordinates;
- compositor-derived trusted-presentation classification.

The result intentionally does not need to be forwarded wholesale to applications. A platform input service can use owner/surface/frame identity for authorization and deliver only the permitted surface-local semantic event to the target process.

Tying the hit to `frame_sequence` lets future delivery policy reason about the exact UI frame the compositor considered visible when the event was targeted. Buffer invalidation removes `has_frame`, so a surface whose presented pixels were revoked cannot continue receiving hits for an image the compositor no longer presents.

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
- performs no polling;
- owns no background gesture recognizer;
- operates over the existing bounded 64-surface compositor scene and 256-node semantic snapshot;
- performs input work only when the platform has an input event to route.

## Security boundary

This is still not the complete hardware input service. The current work establishes both ends of the trusted seam: compositor-authoritative target localization and application-side semantic routing.

The eventual privileged transport must authenticate its endpoint/peer authority, keep `/dev/input`, seat/device state and calibration private, preserve surface generation/owner/frame identity long enough to reject stale delivery, and ensure an event cannot be redirected to another process by application-supplied target IDs.

## Visual/UX relationship

The routing rules intentionally track the same scene/semantic ordering used by the compositor and renderer. This reduces the risk that the user sees one surface/control as topmost while input is delivered to a different lower target.

The mechanism does not prescribe ENML's visual appearance. Authored curves, materials, motion and typography remain renderer concerns; input consumes authoritative geometry and semantic action authority rather than copying another platform's gesture model.

## Not yet claimed

This slice does not yet provide:

- multitouch/gesture recognition;
- pointer capture;
- drag/drop;
- scrolling physics;
- keyboard/navigation focus traversal;
- IME/text-edit input;
- hardware key mapping;
- the authenticated cross-process input transport/service endpoint;
- raw touch calibration or device discovery.

Those must be added as bounded, explicit platform contracts rather than smuggled into the semantic UI layer.
