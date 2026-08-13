# Cookie Network Security Foundation

Cookie networking follows one rule above all others: **moving packets does not grant visibility into the operating system**.

The Wi-Fi, cellular, Ethernet, Bluetooth-PAN and future network-device drivers are recovery domains and hardware authorities, not global observers. They may access only their device MMIO/interrupt/DMA resources and explicitly granted packet rings. They do not receive filesystem, process-table, Key Service, Storage, shell, compositor or unrelated application authority.

## Separation of responsibilities

Cookie splits networking into separately supervised user-space domains:

1. **Link drivers** own one hardware interface and its bounded DMA queues.
2. **Link-security service** owns association/authentication state such as 802.11/WPA and receives only the minimum credential-derived material required by the selected protocol; long-lived profile secrets remain in Key Service.
3. **Packet switch** moves opaque packet buffers between interface and protocol domains according to capability grants. It does not inspect process memory or Storage.
4. **Protocol service** implements IP/transport state and receives packet buffers, not hardware MMIO.
5. **Resolver service** handles DNS separately so DNS policy/privacy can be replaced or restarted independently.
6. **Connection broker** is the application-facing authority. Apps request outbound/listening flows by capability and never receive raw interface authority unless explicitly granted by policy.
7. **Privacy services** such as VPN/relay/private DNS are separate domains with only the flow visibility required for their function.

A single all-seeing networking daemon is prohibited.

## Packet ownership and metadata minimization

Each packet buffer is an explicit capability with direction, interface, owner flow, maximum length and lifetime. A driver sees link-layer framing and the DMA buffer needed for transmission/reception. It is not told the application's principal identity unless a concrete policy requires it. The protocol service sees network/transport headers but does not gain application filesystem or process memory authority. The connection broker knows principal-to-flow authorization but need not handle plaintext application payloads.

Diagnostics default to counters and bounded error codes. Payload capture and full packet tracing require an explicit privileged diagnostic capability, owner authentication and a visible time-bounded session. There is no ambient tcpdump-equivalent authority in production.

## DMA and device compromise

Network drivers run outside Cookie Kernel. Each network device receives only explicitly mapped MMIO ranges, interrupts and IOMMU-confined DMA regions. A compromised Wi-Fi driver must not be able to DMA across general RAM. Packet rings are dedicated, bounded and revoked when the driver generation exits.

## Zero trust inside the phone

Cookie does not trust a component because it is "system" or because traffic originated on a local network. Every cross-domain operation requires explicit authority. Network location is never an authorization credential.

## ZKP research boundary

Zero-knowledge proofs are **not** used for ordinary packet transport, encryption, WPA association or TCP/IP. They may become useful only where Cookie needs to prove a narrowly defined property without revealing a stable identity or additional attributes, for example:

- proving membership in an approved-device/user set to a network access service without exposing the exact member identity;
- privacy-preserving attestation that a device is running an approved Cookie security state without disclosing unrelated device/profile identifiers;
- anonymous rate-limited network access credentials where the verifier needs eligibility but not identity.

Any ZKP deployment must specify the statement proved, leakage, unlinkability goal, replay resistance, revocation design, proof size/latency/resource bounds, trusted setup assumptions if any, and a non-ZKP fallback/recovery story. ZKPs must never become a general-purpose substitute for transport encryption or capability authorization.

## Reference lessons

- QNX demonstrates separating applications from a message-mediated networking subsystem, but Cookie will avoid putting drivers and the full protocol stack into one shared address-space authority because that increases compromise blast radius.
- Android's app sandbox demonstrates that network permission should not imply cross-application or filesystem authority; Cookie carries that principle down into the network services themselves.
- Apple's entitlement-gated Network Extension model demonstrates separating packet tunnel, DNS proxy and content-filter authority rather than treating all networking customization as one permission; Cookie uses finer capability objects rather than vendor entitlements.
- NIST Zero Trust guidance reinforces that neither network location nor device ownership is implicit authorization.

## Non-negotiable tests

Future network milestones must demonstrate at minimum:

- a compromised/malicious driver cannot read unrelated physical memory through DMA;
- a driver cannot open Storage/Key/Shell/Compositor capabilities it was not granted;
- one application's flow capability cannot access or enumerate another application's flows;
- service restart revokes old packet-ring and interface-generation capabilities;
- packet tracing is absent unless explicit diagnostic authority is active;
- malformed packet/ring metadata fails closed without kernel corruption;
- resource exhaustion by one interface/flow remains bounded;
- recovery of Wi-Fi/network domains does not require a device reboot unless hardware or kernel state cannot be recovered safely.
