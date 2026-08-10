# M3.2 — brokered accessibility session transport

This slice moves ENML accessibility from an in-process semantic authority seam toward a real cross-process capability path without adding framebuffer scraping, a global application-controlled accessibility bus or an idle polling worker.

## Layering

The transport is split deliberately:

`SemanticTree`
→ `AccessibilityBridgeAuthority`
→ bounded `core/osaccessibility` records
→ private `AccessibilitySessionServer` / `AccessibilitySessionClient` RPC channel
→ App Manager capability brokering
→ trusted accessibility-service lifecycle.

`core/osui` remains independent of IPC. `core/osaccessibility` sits above `osui` and `osipc`, so wire-format concerns do not leak into the semantic tree, renderer or application-facing UI semantics.

## Bounded record contract

Version 1 defines fixed request/action headers and a bounded snapshot response. The semantic maximum remains 256 nodes and each label remains bounded by the existing semantic text limit, so the worst-case snapshot stays below the existing 64 KiB inline IPC ceiling.

Snapshot decoding revalidates protocol version/size, session/revision, UTF-8, known roles/state/action bits, logical geometry, unique node ids, exactly one root, parent references, cycle freedom and semantic depth. The transport therefore does not assume that a peer is well formed merely because it owns a private endpoint.

Actions remain revision-bound and are re-authorized through `SemanticTree`. Editable text remains intentionally unsupported until caret/selection/privacy/IME semantics are frozen.

## Private application session capability

An application does not publish an accessibility socket name or choose the accessibility-service identity. It asks its already-authenticated post-READY App Manager runtime session for an accessibility capability using a request that carries only protocol version/size.

App Manager first validates the runtime-session packet against the authoritative `ServiceBroker` identity and kernel `SCM_CREDENTIALS`. Only then does it mint:

- a fresh nonzero accessibility session id;
- one local `SOCK_SEQPACKET` pair;
- the application endpoint returned over the authenticated runtime session;
- the service-side endpoint retained by App Manager for privileged claim.

The application request contains no target `PeerIdentity`, `ApplicationInstanceId`, service principal, native descriptor or global process selector.

## Trusted service claim

`ApplicationManager::take_accessibility_endpoint()` is the one-shot authority seam for the service-side capability. Its caller identity must already be resolved by trusted supervisor/runtime state and its principal must equal the canonical ENML accessibility-service principal. The target must exactly equal a live application `PeerIdentity`.

Authorization runs before target lookup so an ordinary caller cannot use target-specific errors to enumerate pending accessibility sessions. A wrong target does not fall through to another running application. A successful claim returns the exact application identity, session id and move-only private channel; a second claim fails because App Manager no longer owns that capability.

This seam is not itself a public service API. The eventual supervised accessibility-service control endpoint must provide the resolved caller identity; applications must never be allowed to manufacture the `caller` argument from payload fields.

## Private session RPC

`AccessibilitySessionServer` runs beside the owning application semantic tree. `AccessibilitySessionClient` runs on the trusted-service side of the App Manager-brokered channel.

The RPC namespace is private to that capability and supports only:

- snapshot request/response;
- semantic accessibility action request/response.

The server is constructed with the exact `AccessibilityBridgeAuthority` for the application session and the trusted caller identity established by brokering. It opens no global listener and performs no identity lookup from application-provided principal fields.

A focused cross-process socketpair test exercises snapshot and focus-action round trips through the client/server channel. The lower transport tests independently cover principal denial, session mismatch, stale revision and malformed semantic graph data.

## Resource and power discipline

This design adds no permanent worker, scanner or polling loop. Snapshot/action work occurs only when a request is made. Serialization uses caller-owned bounded buffers. App Manager only brokers a move-only capability; it is not placed in the synchronous snapshot/action data path, so a slow application accessibility server cannot block App Manager's lifecycle loop.

## Remaining integration work

Before M3.2 can treat accessibility transport as complete, the supervised accessibility-service lifecycle/control path still needs to obtain the App Manager endpoint using a supervisor-resolved accessibility-service `PeerIdentity`, and an application-runtime integration gate needs to prove the exact application/session capability handoff end to end.

Later accessibility product work includes speech, braille, switch control, live-region policy, secure-screen redaction and editable-text integration. Those features must build on the semantic/session authority above rather than bypass it with pixels or raw input APIs.
