# Cookie Kernel-Enforced Network Blindness Layer

## Goal

Cookie should expose the minimum information physically/protocol-wise required to move traffic, and no more. A serving Wi-Fi AP or cellular operator inevitably observes attachment plus some timing/volume information. Cookie must not falsely claim otherwise. Above that unavoidable link layer, ordinary Internet traffic should reveal neither Cookie app identity, process/user identity, DNS names, plaintext application traffic, nor direct destination metadata to the hardware/link domain.

## Enforcement split

Cookie Kernel enforces authority separation; it does not implement DNS, TLS, QUIC, relay protocols, or zero-knowledge proof systems.

For normal Internet traffic, the kernel/device boundary admits only packet capabilities carrying ciphertext for an approved privacy tunnel. A link driver must never receive app identity or destination authority metadata. Raw/direct Internet packet authority is unavailable to ordinary applications and network drivers.

User-space security domains implement encrypted resolution, ECH/TLS/QUIC, relay selection, and privacy-preserving authorization. They are separately supervised and can be restarted/revoked without rebooting Cookie.

## Maximum privacy path

1. An app authenticates only to the Cookie Connection Broker.
2. The broker issues a one-shot resolver grant without PrincipalId/UserId/ProcessId.
3. DNS uses an encrypted/oblivious resolver topology.
4. The resolved destination is represented by an opaque destination capability rather than copied to the link driver.
5. Internet traffic enters an authenticated encrypted tunnel to an ingress privacy relay.
6. A separately selected egress/gateway learns the final destination but not the original client network address.
7. The ingress learns the source network address required to receive packets but not the protected destination/application plaintext.
8. Relay authorization should use unlinkable tokens where possible. Privacy Pass/VOPRF-style mechanisms are the preferred standardized direction; a ZKP may be used only for a specifically reviewed statement such as eligibility or approved-device-state membership.
9. Link hardware sees only its unavoidable local attachment state and encrypted tunnel traffic.

## ZKP boundary

A ZKP cannot hide the fact that a radio is attached to a carrier/AP, nor can it make routing possible without network addresses. ZK is useful for replacing stable authentication identifiers with proofs of a narrow property. Candidate Cookie statements include:

- "holder is authorized to use the privacy relay" without revealing a persistent device identity;
- "device is in an approved Cookie security-state set" without revealing serial/profile identity;
- "holder possesses a valid subscription/entitlement token" where the service supports privacy-preserving issuance/redemption.

Proof systems must remain outside Cookie Kernel. They require a separate cryptographic review covering linkability, replay, revocation, side channels, proof size/CPU/memory bounds, setup assumptions, and fallback behavior.

## Telecom limitation

The OS cannot by itself make a cellular carrier unaware of the subscriber relationship required by the carrier's authentication/billing/routing system. Cookie can minimize identifiers above the modem boundary, restrict baseband authority, prefer privacy-preserving cellular identity mechanisms provided by the radio/network generation, and tunnel application traffic so the carrier does not directly receive app/DNS/plaintext destination metadata beyond traffic-analysis and tunnel-endpoint information.

## Mandatory invariants

- ordinary Internet traffic cannot bypass the approved encrypted tunnel in maximum mode;
- link/driver authority never contains app identity;
- link/driver authority never contains direct destination metadata;
- resolver APIs need not receive ordinary process/user identity;
- no cleartext DNS or application fallback in maximum mode;
- ECH is required when a valid configuration is available and failure cannot be mislabeled as protected;
- stale flow/tunnel capabilities are revoked on recovery generation change;
- no ambient packet capture or stable OS network identifier;
- emergency telephony may use a narrowly scoped direct exception without granting general app Internet bypass;
- diagnostics clearly distinguish unavoidable attachment/timing/volume leakage from higher-layer privacy guarantees.

## Reference basis

Cookie follows the security properties of TLS ECH (RFC 9849), Oblivious DoH (RFC 9230), MASQUE IP/UDP proxying (RFC 9484/RFC 9298), and Privacy Pass (RFC 9576). These standards inform the protocol/privacy properties; Cookie does not import their implementation topology into the kernel.
