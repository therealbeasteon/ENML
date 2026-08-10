# M3.2 — brokered accessibility session transport

This slice moves ENML accessibility from an in-process semantic authority seam to a supervised cross-process capability path without adding framebuffer scraping, a global application-controlled accessibility bus or an idle polling worker.

## Layering

The transport is split deliberately:

`SemanticTree`
→ `AccessibilityBridgeAuthority`
→ bounded `core/osaccessibility` records
→ private `AccessibilitySessionServer` / `AccessibilitySessionClient` RPC channel
→ App Manager capability brokering
→ authenticated accessibility claim control
→ supervised `system.accessibility` lifecycle.

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

## Authenticated claim control

`AccessibilityBrokerControlServer` provides the App Manager RPC boundary above the in-process claim seam. It receives the request packet first, runs the normal `validate_rpc_request()` path, and resolves the sender's kernel `SCM_CREDENTIALS` through a trusted `PeerIdentityResolver`. The request payload contains **only** the exact target application `PeerIdentity`; it contains no caller principal, caller process identity or native descriptor that could be used to manufacture authority.

The resolved caller must have `accessibility_service_principal` before the target payload is decoded or the App Manager claim callback is entered. This preserves the anti-enumeration property across the process boundary.

The neutral client/wire half now lives in `core/osaccessibility/broker.hpp` rather than forcing `system.accessibility` to link the full App Manager/package/storage stack. On success the control server sends fixed metadata containing the nonzero session id and exact application identity plus exactly one move-only channel transferred with `SCM_RIGHTS`.

## Supervised accessibility service

`system.accessibility` is now a real Supervisor-managed process with the canonical `accessibility_service_principal`.

The Supervisor gained one generic **private service capability** slot. Trusted composition may provide a borrowed capability descriptor in `ServiceLaunchConfig`; each service generation receives only a duplicate at private fd 6. The descriptor is never returned by `Supervisor::connect()` and is never published through the application `ServiceBroker`. `system.accessibility` uses this fd exclusively as its App Manager broker-control channel.

The service still receives the normal private Supervisor control fd for bootstrap and identity publication. Its administration endpoint is intentionally not registered for applications in M3.2. A trusted caller must both possess the Supervisor-owned endpoint capability and resolve through the service's generation-local `IdentityRegistry` to `accessibility_admin_principal`. Merely inheriting/obtaining an endpoint without that published identity is insufficient.

The service keeps at most 16 claimed application sessions. There is no dynamically growing session map. Each slot contains the exact application `PeerIdentity`, nonzero session id, move-only application channel and a persistent `AccessibilitySessionClient`, so synchronous RPC request ids continue monotonically on that capability. Peer death clears the slot rather than retaining stale session state.

The private administration operations are bounded to claim, snapshot, semantic action and release. Claim authority still comes from the separate App Manager broker: the administration caller cannot manufacture an application session by supplying a descriptor or session id.

## Private session RPC

`AccessibilitySessionServer` runs beside the owning application semantic tree. `AccessibilitySessionClient` runs in `system.accessibility` on the App Manager-brokered channel.

The RPC namespace is private to that capability and supports only:

- snapshot request/response;
- semantic accessibility action request/response.

The server is constructed with the exact `AccessibilityBridgeAuthority` for the application session and the trusted caller identity established by brokering. It opens no global listener and performs no identity lookup from application-provided principal fields.

## Lifecycle validation

`accessibility_service_supervisor_test` launches the real `system.accessibility` binary under `Supervisor`, injects only the private broker capability, publishes an authenticated admin identity, and proves the following path:

`trusted admin`
→ supervised accessibility service
→ authenticated broker claim made by the service process
→ exactly one transferred application session channel
→ semantic snapshot
→ revision-bound focus action
→ application `SemanticTree` state change
→ explicit session release.

The same fixture then republishes the exact native admin sender under an ordinary principal and proves that the already-open Supervisor endpoint is no longer enough: the service returns `authority_denied` before touching the now-unavailable broker. The test is part of all four M3 UI runners, including native AArch64 and ASan/UBSan.

A focused lower-level broker test separately proves that a non-accessibility principal which knows the private broker service id and exact target identity is denied before the App Manager claim callback is invoked.

## Resource and power discipline

This design adds no permanent worker, scanner or polling loop. `system.accessibility` blocks in the same event-driven `poll()` style as other small supervised services and wakes only for Supervisor control or an explicit trusted administration request. Snapshot/action and broker work is synchronous and bounded; serialization uses caller-owned fixed buffers.

App Manager brokers a move-only capability but is not placed in the synchronous snapshot/action data path, so a slow application accessibility server does not make App Manager a semantic relay or create an unbounded producer queue.

## Remaining integration/product work

The supervised lifecycle and authenticated claim path now exist. A broader application-runtime integration fixture can still combine the already-tested application capability request and the supervised service path in one test, but no new authority mechanism is required for that composition.

Later accessibility product work includes speech, braille, switch control, live-region policy, secure-screen redaction and editable-text integration. Those features must build on the semantic/session authority above rather than bypass it with pixels or raw input APIs.
