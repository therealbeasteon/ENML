# M3.2 — bounded semantic input routing

This slice establishes the first deterministic pointer-to-semantic-action boundary for ENML UI.

## Purpose

Applications should reason about semantic actions such as activate, focus, toggle and select. They should not receive Linux input device descriptors, evdev structures, compositor-private hit-test state or raw hardware coordinates.

A future hardware/input adapter is responsible for converting device-specific input into ENML logical Q6 coordinates. `osui` then resolves that point against an immutable semantic `RendererSnapshot`.

## Routing contract

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

The router:

- allocates no heap memory;
- creates no worker or timer thread;
- performs no polling;
- owns no background gesture recognizer;
- operates over the existing bounded 256-node snapshot and depth-16 hierarchy.

Input work therefore happens only when the platform has an input event to route.

## Security boundary

This is not yet the hardware input service. It is the semantic boundary that such a service can call later.

The eventual platform bridge must authenticate its endpoint/peer authority, normalize coordinates into the surface-local logical space, and keep `/dev/input`, seat/device state, compositor internals and hardware-specific gesture mechanisms private to trusted platform components.

## Visual/UX relationship

The routing rules intentionally track the same semantic hierarchy and ordering that the renderer uses. This reduces the risk that the user sees one control as topmost while input is delivered to a different lower control.

The mechanism does not prescribe ENML's visual appearance. Authored curves, materials, motion and typography remain renderer concerns; input consumes semantic geometry and action authority rather than copying another platform's gesture model.

## Not yet claimed

This slice does not yet provide:

- multitouch/gesture recognition;
- pointer capture;
- drag/drop;
- scrolling physics;
- keyboard/navigation focus traversal;
- IME/text-edit input;
- hardware key mapping;
- a privileged input daemon/service;
- raw touch calibration or device discovery.

Those must be added as bounded, explicit platform contracts rather than smuggled into the semantic UI layer.
