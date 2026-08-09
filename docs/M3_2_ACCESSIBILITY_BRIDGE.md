# M3.2 — revision-bound semantic accessibility bridge

This slice turns the existing fixed-capacity accessibility projection into a service-ready semantic seam without introducing framebuffer scraping, OCR, background polling or application access to privileged accessibility-service internals.

## Snapshot contract

`accessibility_service_snapshot()` captures:

- the exact `SemanticTree` revision;
- the existing bounded `AccessibilitySnapshot` projection;
- semantic role, bounds, state, actions and validated labels rather than renderer pixels.

One revision therefore names one coherent accessibility view of the tree. Platform accessibility code can cache or transport that bounded snapshot, but any later action request must identify the revision from which the target was obtained.

## Action contract

`dispatch_accessibility_action()` accepts a revision-bound `AccessibilityActionRequest` and re-authorizes the request against the live `SemanticTree`.

The boundary rejects:

- revision zero;
- a request from any stale semantic revision;
- removed or forged `UiNodeId` values;
- accessibility-hidden direct targets;
- semantic actions not currently supported by this bridge.

Focus is applied through `SemanticTree::focus()`, preserving unique-focus, enabled and effective-visibility invariants. Activate/toggle/select actions reuse `SemanticTree::dispatch_action()` rather than creating a parallel authorization model.

## Trusted service authority

`AccessibilityBridgeAuthority` is the in-process authorization seam for the eventual cross-process accessibility transport. It binds one live `SemanticTree` to one supervisor-assigned trusted accessibility `PrincipalId`.

A protocol/service implementation should pass its already-resolved `PeerIdentity` through this authority rather than duplicating access checks in wire-code handlers. Ordinary application principals cannot request privileged snapshots or dispatch accessibility actions through the authority merely because they know a node ID.

The authority still delegates snapshot creation and action dispatch to the same revision-bound functions above, so adding service authorization does not create a second semantic state machine.

Principal-level authorization is intentional at this seam: a supervised accessibility service may restart with a fresh `ProcessId` while retaining its trusted principal. The eventual service endpoint must still use the existing kernel-credential/identity-registry machinery to prove that the caller really owns that principal.

## Editable text is deliberately deferred

`UiAction::set_text` is not accepted by this bridge yet. Accessibility text editing needs a bounded editable-text payload, selection/caret semantics, privacy rules and the eventual IME/text-input contract. Accepting a payload-free `set_text` action now would freeze an incomplete API and encourage a second ad-hoc text channel.

## Resource and power discipline

The bridge:

- uses the existing fixed 256-node semantic capacity;
- allocates no unbounded accessibility tree;
- creates no worker/timer thread;
- performs no polling;
- does not inspect framebuffer pixels;
- performs work only when a snapshot is requested or an accessibility action arrives.

A future platform accessibility service may subscribe to semantic revision/change notification, but it must remain event-driven and bounded rather than repeatedly scanning UI state.

## Security and privacy

The semantic tree remains the source of accessibility meaning. Rendering effects, translucency, shader output and application-drawn lookalike pixels cannot manufacture accessibility authority.

The future cross-process accessibility transport must authenticate the privileged accessibility-service principal through trusted process/kernel state, scope requests to the owning application/session, preserve revision identity, and avoid exposing labels or text from unrelated users/sessions. Secure-system presentation may require stricter policy than ordinary application surfaces.

## Not yet claimed

This slice does not yet provide:

- the privileged accessibility daemon/service transport;
- screen-reader speech output;
- switch-control or alternative-input policy;
- editable-text/caret/selection APIs;
- live-region announcement policy;
- braille transport;
- cross-process subscription/change notification;
- secure-screen redaction policy.

Those should be layered above this bounded semantic contract rather than implemented by pixel scraping or vendor-specific accessibility APIs inside `osui`.
