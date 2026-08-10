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

The authority is also bound to a nonzero `AccessibilitySessionId`. Snapshot responses carry that session id and action requests must return the same session id. A semantically valid revision/node/action tuple therefore cannot be replayed against another runtime session simply because numeric node IDs happen to overlap.

Principal authorization is deliberately evaluated before reporting a requested session mismatch. An unauthorized caller should not be able to probe whether a guessed accessibility session exists by observing different session errors.

## Bounded transport records

`core/osaccessibility` now sits above `osui` + `osipc`. This keeps the semantic UI core itself free of IPC dependencies while defining the fixed records a later endpoint/broker can carry.

Version 1 provides bounded codecs for:

- snapshot request: version/size + exact `AccessibilitySessionId`;
- action request: session + semantic revision + `UiNodeId` + `UiAction`;
- snapshot response: session + revision + bounded semantic node projection;
- action response: the resulting semantic `UiEvent`.

The maximum snapshot response remains below the existing 64 KiB inline IPC ceiling even when all 256 semantic nodes carry 160-byte labels. Encoding is variable-length only with respect to each already-bounded label; there is no unbounded vector, pointer-bearing record or heap-owned serialized tree.

Snapshot decoding independently validates:

- protocol version and exact encoded size;
- nonzero session/revision and node count within 256;
- known roles, action bits and state bits;
- bounded logical geometry and validated UTF-8 labels;
- nonzero/unique node IDs;
- exactly one semantic root;
- valid parent references;
- no self-parenting or parent cycles;
- parent-chain depth no greater than the semantic tree limit.

A transport consumer therefore does not need to trust a malformed peer simply because that peer was expected to be an application runtime.

`dispatch_accessibility_transport_v1()` is transport-neutral: an outer capability/service layer supplies an already-resolved `PeerIdentity`, then the dispatcher decodes the fixed request, applies `AccessibilityBridgeAuthority` principal/session/revision/node/action checks, and writes a bounded response into caller-owned memory. This avoids freezing endpoint-brokering policy inside `osui` or duplicating semantic authorization in wire handlers.

Focused tests cover valid snapshot/action round trips, trusted-vs-ordinary principal behavior, session mismatch, stale revision rejection, structurally invalid graphs and malformed duplicate-node wire data. The same gate runs in the M3 Semantic UI matrix.

## Why endpoint brokering is still separate

Unlike compositor input targeting, accessibility state lives inside each application runtime rather than in a single global compositor scene. The eventual cross-process path therefore needs explicit endpoint/session ownership: a trusted accessibility service may address only the application/session endpoint that owns the tree, and the application-side adapter must still re-check snapshot revision and target/action validity against its live tree.

The new record/dispatcher layer intentionally does **not** assume that arbitrary applications can resolve supervisor identities, nor does it turn a service ID or a claimed principal field into authority. App Manager/supervisor brokering must establish the private endpoint and authenticated caller context; only then should the transport dispatcher be invoked.

## Editable text is deliberately deferred

`UiAction::set_text` is not accepted by the semantic bridge yet. Accessibility text editing needs a bounded editable-text payload, selection/caret semantics, privacy rules and the eventual IME/text-input contract. Accepting a payload-free `set_text` action now would freeze an incomplete API and encourage a second ad-hoc text channel.

## Resource and power discipline

The bridge and transport records:

- use the existing fixed 256-node semantic capacity;
- allocate no unbounded accessibility tree;
- serialize into caller-owned bounded buffers;
- create no worker/timer thread;
- perform no polling;
- do not inspect framebuffer pixels;
- perform work only when a snapshot/action request arrives.

A future platform accessibility service may subscribe to semantic revision/change notification, but it must remain event-driven and bounded rather than repeatedly scanning UI state.

## Security and privacy

The semantic tree remains the source of accessibility meaning. Rendering effects, translucency, shader output and application-drawn lookalike pixels cannot manufacture accessibility authority.

The future endpoint broker must authenticate the privileged accessibility-service principal, scope requests to the exact owning application/session, preserve revision identity, and avoid exposing labels or text from unrelated users/sessions. Secure-system presentation may require stricter policy than ordinary application surfaces.

## Not yet claimed

This slice does not yet provide:

- the App Manager/supervisor-brokered cross-process application accessibility endpoint;
- a complete supervised screen-reader/accessibility daemon;
- screen-reader speech output;
- switch-control or alternative-input policy;
- editable-text/caret/selection APIs;
- live-region announcement policy;
- braille transport;
- secure-screen redaction policy.

Those should be layered above the bounded semantic/record contracts rather than implemented by pixel scraping or vendor-specific accessibility APIs inside `osui`.
