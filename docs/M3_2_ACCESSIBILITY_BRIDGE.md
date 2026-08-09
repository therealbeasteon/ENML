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

## Trusted accessibility authority

`AccessibilityBridgeAuthority` adds the privileged identity boundary above the semantic operations. A caller must have a valid runtime `PeerIdentity` whose principal matches the configured trusted accessibility principal before it can request a semantic snapshot or dispatch an accessibility action.

This mirrors the security shape now used by the compositor input bridge: transport code should authenticate the peer through supervisor/runtime identity first, then call the authority object. The eventual wire service must not duplicate authorization logic or accept an application-provided principal as proof of authority.

Unlike compositor input targeting, accessibility is application-runtime state rather than global compositor state. The final transport therefore needs explicit **session ownership**: a trusted accessibility service may address only the application/session endpoint that owns the tree, and the application endpoint must still re-check snapshot revision and target/action validity against its live tree.

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

The future cross-process transport must authenticate the privileged accessibility-service principal, scope requests to the exact owning application/session, preserve revision identity, and avoid exposing labels or text from unrelated users/sessions. Secure-system presentation may require stricter policy than ordinary application surfaces.

Snapshot transport should remain bounded to the existing semantic capacity or a smaller explicit message window. It must not introduce an unbounded serialized tree, pointer-bearing ABI, framebuffer fallback, or a background poller just because the accessibility consumer is cross-process.

## Not yet claimed

This slice does not yet provide:

- the cross-process application/session accessibility endpoint and subscription transport;
- screen-reader speech output;
- switch-control or alternative-input policy;
- editable-text/caret/selection APIs;
- live-region announcement policy;
- braille transport;
- secure-screen redaction policy.

Those should be layered above this bounded semantic contract rather than implemented by pixel scraping or vendor-specific accessibility APIs inside `osui`.
