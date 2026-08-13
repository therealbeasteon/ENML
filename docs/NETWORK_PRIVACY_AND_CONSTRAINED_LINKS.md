# Cookie Network Privacy and Constrained-Link Architecture

Cookie's networking objective is **minimum observable information, minimum unnecessary traffic, and useful performance on every viable link**. The design does not promise that a carrier or access point can be made unaware that a device is attached: radio association, local link delivery, timing and aggregate byte counts are routing facts. Cookie instead minimizes and partitions everything above that unavoidable layer.

## Privacy layers

Cookie uses multiple independent protections rather than treating one VPN as a complete privacy solution.

1. **Link protection** secures the local radio/link protocol where supported. Link credentials remain in the smallest link-security domain possible.
2. **Encrypted resolver** is mandatory for normal operation. Zero-tracking mode prefers an oblivious resolver topology so no single resolver-side party learns both client address and DNS question.
3. **TLS 1.3 or stronger** is the normal application-transport floor. Cleartext fallback is not permitted by the zero-tracking policy.
4. **Encrypted Client Hello (ECH)** is required when the selected service advertises a usable configuration. Cookie must not silently pretend ECH succeeded when it did not.
5. **Split privacy relay** is used by zero-tracking mode when configured: the ingress learns the client's network address but not the protected destination content, while the egress/gateway learns the destination but not the client's source address. The two roles must be operated and keyed so one compromised role does not trivially recover the complete mapping.
6. **Privacy-preserving authorization** may use unlinkable tokens/Privacy-Pass-style protocols when a service needs proof of eligibility without a stable per-device identifier. A ZKP or VOPRF is used only for a defined authorization statement, never as a replacement for packet encryption.

Cookie deliberately avoids a permanent OS advertising identifier for networking. Per-network link identifiers, discovery identifiers and relay credentials must be scoped and rotated according to their protocol requirements. Rotation must not break emergency calling, captive-network recovery or explicitly trusted enterprise identity flows; those exceptions require visible policy rather than hidden global identifiers.

## Limits of concealment

Even with ECH, encrypted DNS and relays, an observer may still infer information from destination IP ranges, packet sizes, timing, radio-cell attachment, mobility, total bytes and active/inactive periods. Padding can reduce some traffic analysis but consumes scarce bandwidth and power, especially on GPRS/EDGE-class links. Cookie therefore treats padding as a bounded privacy budget rather than enabling unlimited cover traffic.

No component may report "anonymous" or "untrackable" merely because encryption is enabled. UI and diagnostics must describe the active protections precisely.

## Constrained-link performance

Cookie does not optimize by labels such as 2G, 3G, 4G or 5G. It continuously classifies the live path from bounded measurements including smoothed RTT, available throughput, loss, metering and energy cost. The same policy therefore works for EDGE, GPRS, congested LTE/5G, satellite, weak Wi-Fi and future radios.

On constrained/high-latency paths Cookie should:

- keep background concurrency extremely small so synchronization traffic cannot fill queues ahead of interactive work;
- prioritize user-initiated interactive flows over update, telemetry and background refresh traffic;
- reuse authenticated secure connections where privacy policy permits, reducing repeated handshake cost;
- prefer QUIC/HTTP/3 when validated for the peer/path because multiplexed streams avoid TCP-level head-of-line blocking and support low-latency connection establishment, but retain an encrypted interoperable fallback when UDP/QUIC is blocked or performs worse;
- use per-path congestion/loss state and never infer capacity from the radio generation name;
- suppress speculative prefetching on scarce links unless explicitly requested by the user or proven beneficial within a bounded budget;
- coalesce small background operations to reduce radio wakeups, header overhead and contention;
- apply content compression only before encryption and only to formats/protocols where compression does not create a cross-secret side channel;
- avoid generic TLS 0-RTT. A future request API may opt in only for operations explicitly classified replay-safe;
- maintain small bounded queues to limit bufferbloat rather than maximizing queued bytes;
- use ECN where the path validates it and treat network-provided congestion signals as untrusted input to bounded congestion-control logic.

The goal is not to claim 2G can have 5G capacity. The goal is to reduce protocol/OS waste so useful latency and goodput approach the physical link's real capability.

## No-tracking operating policy

Zero-tracking mode has these mandatory properties:

- no cleartext DNS;
- no cleartext application fallback for Internet connections unless the owner explicitly leaves zero-tracking mode for a named connection;
- no ambient packet capture;
- no stable OS networking identifier exposed to ordinary applications or remote services;
- no cross-application connection enumeration;
- resolver queries use oblivious mode when a configured compatible service is available;
- eligible Internet flows use split-relay privacy when configured and healthy;
- ECH is attempted whenever a valid service configuration is available;
- diagnostic logs contain bounded counters/errors, not URLs, DNS names, payloads or full IP-flow histories by default;
- network-state history is ephemeral unless the owner explicitly enables a bounded diagnostic session.

## Cryptographic research boundary

Cookie tracks IETF-standardized or interoperable mechanisms first. Current reference points include TLS ECH, QUIC/HTTP/3, Oblivious HTTP, Oblivious DNS over HTTPS and Privacy Pass. Experimental cryptography may only replace a standardized mechanism after a separate threat-model, interoperability, resource, side-channel and cryptographic review.

For privacy-preserving network admission, Cookie may investigate VOPRF/Privacy-Pass-style unlinkable tokens and carefully scoped zero-knowledge proofs. The proof must reveal no stable device identifier unless that is explicitly the statement being proved. Proof verification belongs outside Cookie Kernel in a bounded privacy-authentication service.

## Mandatory future tests

Network implementation is not mature until tests demonstrate at minimum:

- downgrade from required encrypted transport fails closed;
- failed/stripped ECH cannot be reported as ECH-protected;
- oblivious DNS separation does not expose both client address and plaintext query to one Cookie service;
- privacy relay ingress cannot read protected destination content and relay egress cannot obtain the original client source identity through the Cookie protocol;
- no ordinary app or network driver can request a stable global device-network identifier;
- constrained-link policy caps background flows and preserves an interactive-flow budget;
- switching between a congested modern radio and a strong legacy radio follows measured path properties rather than radio labels;
- restart of any network recovery domain revokes old packet/routing capabilities and privacy-session credentials;
- loss of the privacy relay does not silently downgrade zero-tracking mode to cleartext or identifier-rich traffic;
- diagnostics remain metadata-minimal unless an authenticated, visible, time-bounded capture session is active.
