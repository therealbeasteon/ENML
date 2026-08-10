# M3.2 — exact-owner application input delivery

This slice completes the first bounded transport path from compositor-authorized input targeting into the exact owning application runtime without exposing global display coordinates, Linux input devices or application-selected target authority.

## End-to-end authority chain

The implemented path is now:

`trusted hardware/input adapter`
→ authenticated compositor hit-test RPC
→ compositor-owned `SurfaceInputHit`
→ immediate pre-delivery compositor revalidation
→ trusted runtime constructs `ApplicationInputEventV1`
→ App Manager exact-`PeerIdentity` routing
→ private per-application event capability
→ `ApplicationInputEventStream`
→ surface-local event available to the owning application UI runtime.

The compositor remains the authority that chooses the target surface. App Manager does not accept an `ApplicationInstanceId` as redirect authority for input delivery; it looks up the live instance by the exact target `PeerIdentity` carried forward from the compositor-authorized hit.

## Private endpoint acquisition

Bootstrap-v2's long-lived application/runtime session gained a second narrow operation: `acquire_input_events`.

The application request contains only the protocol version/size. It does **not** contain a target process, principal, application instance, surface id or native descriptor. App Manager first authenticates the runtime-session packet using kernel `SCM_CREDENTIALS` and the `ServiceBroker`'s authoritative process record. Only then does it create a fresh local `SOCK_SEQPACKET` pair.

One endpoint is transferred to the application with `SCM_RIGHTS`; App Manager retains the opposite endpoint as the only sender for that live instance. Reacquiring the endpoint replaces the prior sender capability rather than accumulating parallel event channels.

The retained sender is set nonblocking. A stalled application therefore cannot make trusted lifecycle/input code wait indefinitely on an ever-growing event queue. If the bounded kernel queue cannot accept an event or the peer endpoint is dead, delivery fails closed, the sender endpoint is discarded, and the application must explicitly reacquire a fresh endpoint.

## Event record

`ApplicationInputEventV1` is a fixed 84-byte private runtime payload containing:

- monotonic event sequence;
- exact target `PeerIdentity`;
- opaque generation-scoped surface id value;
- presented frame sequence;
- surface pixel width/height;
- surface-local x/y;
- pointer id;
- pointer phase (`down`, `move`, `up`, `cancel`).

It carries no global screen coordinate, device node, evdev code, hardware identifier, compositor z-order, raw file descriptor or application-selected callback pointer.

Structural validation rejects zero identity/surface/frame/sequence values, invalid pointer phases, negative local coordinates and local coordinates outside the carried surface dimensions.

## Replay and cross-process protection

App Manager retains the last successfully delivered input sequence per live application instance. A sequence that is zero, equal to or older than the last delivered sequence is rejected before another packet is queued. This sequence state survives event-endpoint reacquisition.

`ApplicationInputEventStream` independently binds the receive side to the exact bootstrap `PeerIdentity` and rejects a structurally valid event addressed to another runtime identity. It also keeps a receive-side monotonic sequence guard.

The paired checks are intentional: trusted routing rejects misdelivery before send, while the application runtime still verifies that the event arriving on its capability is for its own bootstrap identity.

## Integration coverage

The existing brokered App Manager runtime-session integration now performs the full focused path before its service-restart checks:

1. the child application authenticates over its post-READY runtime session and acquires a private input endpoint;
2. App Manager rejects an event whose target `PeerIdentity` was altered even though only one fixture application is running;
3. App Manager delivers the correctly addressed event;
4. immediate replay of the same sequence is rejected;
5. the child receives and validates the exact identity, surface/frame, surface dimensions, local point, pointer id and phase;
6. the rest of the Storage/Key restart/reacquisition test continues, proving the new event capability does not replace or weaken the existing service-capability model.

Separate unit tests cover the fixed event codec, application-side target mismatch, receive-side replay rejection, out-of-bounds local coordinates and runtime-session endpoint transfer.

## Resource discipline

This transport adds no permanent input thread, no polling worker, no userspace event backlog and no reconnect loop. App Manager services endpoint-acquisition requests through its existing bounded runtime-session work per `maintain()` iteration. Input events are sent only when trusted input work exists.

The event channel uses the kernel's bounded socket queue. Nonblocking trusted sends convert a slow/unresponsive application into an explicit delivery failure rather than allowing it to stall system input/lifecycle work.

## What this does not claim

This slice is still not raw hardware input discovery or a full gesture system. Later contracts must define multitouch arbitration, pointer capture, scrolling/gesture recognition, keyboard navigation, hardware-key mapping and IME/text editing.

The eventual hardware input service must still own `/dev/input`/seat/device state privately and must obtain its principal from trusted supervisor lifecycle state. Those later pieces feed the authority chain above; they do not bypass compositor targeting or App Manager exact-owner delivery.
