# Cookie Demand-Driven Attack Surface

Cookie treats optional subsystem residency as a security decision. A subsystem should not remain running, mapped, DMA-capable, interrupt-capable or powered merely because the device supports it.

## Core model

Applications and trusted system functions acquire bounded leases on recovery domains. A lease names need, not process lifetime. When the final lease disappears, Supervisor evaluates the domain's idle disposition and safety context.

The shutdown transaction is ordered:

1. stop admission of new work;
2. quiesce/drain bounded in-flight work;
3. revoke client capabilities and advance the service generation;
4. revoke hardware DMA mappings and interrupt authority where the domain owns hardware;
5. zero transient credentials/session keys and private scratch state;
6. stop the userspace service/driver when allowed;
7. request the machine/power service to enter the narrowest hardware state compatible with required wake sources.

A later lease creates a new generation and reacquires hardware/resources through normal capability checks. Old clients never regain authority merely because the same subsystem name returned.

## Why this is different from a toggle

A user-visible switch is policy input. It is not itself enforcement. Cookie couples settings, capability lifetime, process lifetime, DMA/IRQ lifetime, secret lifetime and hardware power state into one lifecycle transaction.

This also avoids assuming that every hardware platform exposes identical power states. `power_gate_hardware` is an authorization to the machine/device power authority, not a promise that every board can electrically remove power. A hardware port must report the strongest state it can actually enforce.

## Domain policy

Default optional domains include network, Bluetooth, camera, microphone, location, sensors and USB data. They are candidates for service/driver stop plus hardware power gating after their last lease.

Display and audio can stop higher-level services when idle while preserving a minimal hardware-safe state if platform requirements demand it.

Telephony is special: it may need a minimal always-listening modem/baseband state for emergency calls or paging. Higher protocol/user-facing components can still be quiesced and authority minimized. Emergency operation blocks a transition that would break the call.

Storage and Key Service remain resident security roots for now. Their attack surface is reduced through narrow RPC/capability APIs rather than opportunistic shutdown because other durable-security invariants depend on them. A future hardware-backed key provider may allow deeper idle states without stopping the logical Key Service.

## Privacy consequences

A stopped sensor/camera/network domain cannot service ordinary application requests because the capabilities are gone, not because Settings lies about availability. On reactivation, Cookie issues a fresh generation and fresh transient state.

This lets a high-security profile request policies such as:

- network active only while explicit network leases exist, subject to user-selected push/telephony policy;
- Bluetooth hardware off with no leases;
- camera/microphone driver and DMA authority absent unless foreground capture is authorized;
- USB data hardware disabled while locked or when no authenticated data session exists;
- location/sensor services stopped when no authorized consumer exists.

## Reference lessons

GrapheneOS demonstrates attack-surface reduction through hardware-backed USB data controls, network/sensor permission removal and radio-mode reduction. Cookie generalizes the property to recovery domains and binds it to service/hardware capability lifetime rather than Android permissions.

QNX demonstrates that device/resource-manager processes can be dynamically started and stopped and that microkernel isolation enables component recovery without reboot. Cookie combines this with generation-scoped capabilities and explicit hardware authority revocation.

Apple's sandbox/entitlement model reinforces that access to powerful services should be explicit rather than ambient. Cookie additionally removes idle service/hardware authority when it is no longer needed.

## Non-negotiable validation

- no last-lease transition may power-gate hardware before DMA/IRQ/client capability revocation;
- a stale pre-quiesce capability is rejected after reactivation;
- transient credentials are not retained merely to make restart faster;
- user-space driver exit cannot leave device DMA mapped into general memory;
- security/update/destruction transactions can veto unsafe quiescence;
- emergency telephony requirements override power saving without granting unrelated app authority;
- a required hardware wake source may keep minimal hardware state but must not keep unnecessary app-facing services resident;
- machine ports report unsupported power-gating honestly rather than simulating it.
