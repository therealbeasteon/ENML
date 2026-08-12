# P13 Security & Privacy Hardening — Architecture Preparation

Status: PREPARATION ONLY. Implementation is locked behind the roadmap entry gates. This document defines the threat model and acceptance contract early so security is designed into P8–P12 rather than added at the end.

## Product stance

Cookie OS is security- and privacy-first. Compatibility and convenience do not justify ambient authority, silent privilege expansion, unnecessary persistent metadata, or weakening verified state.

External systems are references for principles, not templates. Android/GrapheneOS demonstrate layered sandboxing, exploit mitigation and verified-state value; Apple demonstrates hardware-rooted boot and separated key/data protection; NIST supplies lifecycle/risk-management guidance. Cookie should implement equivalent goals through its own capability/context architecture rather than inheriting UID/SELinux/POSIX identity as its security model.

## Threat model

P13 must assume at least these attackers:

1. **Malicious application** — validly installed but attempts unauthorized sensor, data, storage, IPC or network use.
2. **Compromised application/service** — memory corruption or logic flaw gives attacker full control of one userspace compartment.
3. **Compromised driver/device-facing service** — attacker controls a high-risk hardware parser or device service and attempts lateral movement or DMA abuse.
4. **Kernel exploit attempt** — attacker seeks arbitrary kernel execution or reuse of stale kernel state.
5. **Physical-loss attacker** — device is stolen while locked or powered off; attacker can inspect removable/external interfaces and reboot it.
6. **Boot/update rollback attacker** — attempts to boot older vulnerable system state or replace executable/system metadata.
7. **Supply-chain/vendor firmware compromise** — a peripheral firmware/BSP component is malicious or outdated.
8. **Privacy adversary** — app, service or remote party attempts behavioral profiling through metadata, identifiers, logs, sensor access or background traffic without needing full compromise.
9. **Recovery/duress adversary** — coerces authentication or abuses recovery paths to obtain normal user state.

No single mitigation is considered sufficient against these classes.

## Cross-system lessons converted into Cookie principles

### Exploit resistance

- Prefer eliminating vulnerability classes before mitigating exploitation.
- Use memory-safe languages/components where practical outside the low-level kernel/driver boundary.
- For freestanding C++/assembly, require strict lifetime, bounds, integer, initialization and control-flow discipline plus compiler/hardware mitigations where available.
- Treat allocator quarantine, guard regions, memory tagging and delayed reuse as candidate defenses where hardware/TCB cost is justified.
- Compromise of one service must not imply authority to every service or device.

### Anti-persistence and verified state

- Establish trust from immutable/hardware-protected boot state where target hardware permits it.
- Authenticate each executable security-critical stage before use.
- Bind update acceptance to rollback-resistant version state.
- Treat verified boot and recovery as one design: verification failure must have a deterministic, authenticated recovery path rather than a privilege bypass.
- Do not trust writable persistent state merely because a previous boot created it.

### Data protection

- Separate data-encryption keys by purpose/security domain instead of one universal device key.
- Tie high-value key release to verified device state plus the required user authorization state.
- Keep raw long-term secrets out of general service address spaces where feasible.
- Make deletion/credential reset semantics explicit so key destruction can make protected data cryptographically inaccessible.

### Privacy

- Collect no telemetry by default solely to make security policy easier to implement.
- Kernel security context contains compact authority identifiers, not user activity history.
- Background sensor/network/data authority is distinct from foreground/interactive authority where policy requires it.
- Logs must be bounded, purpose-limited and redact secrets/content by construction.
- Stable identifiers are capabilities with explicit consumers, not ambient globally readable properties.

## Hardening workstreams

### P13.1 Kernel exploit surface

- audit syscall count and argument surface;
- reject caller-supplied identity; derive caller from live kernel execution state;
- retain generation-bound capabilities, mappings, IPC transactions and continuations;
- validate all user pointers through bounded tickets tied to a live translation generation;
- enforce W^X / non-executable data and immutable executable mappings where architecture permits;
- use guard pages for kernel stacks and critical regions;
- maintain explicit integer-overflow and bounds checks at ABI boundaries;
- fuzz pure kernel state machines on host builds and run sanitizer variants where meaningful;
- use architecture mitigations such as pointer authentication / branch target controls / memory tagging only where target hardware and threat model justify them, without making correctness depend on optional mitigation hardware.

### P13.2 Service compartmentalization

- one service receives only the capabilities required for its declared role;
- parsers for hostile formats/network data are isolated from key/storage authority;
- restart creates fresh authority generations;
- service registry returns capabilities, not ambient names implying access;
- privileged brokers must attenuate delegated capabilities rather than proxy unlimited authority.

### P13.3 Application sandbox

- per-app execution/address-space authority;
- explicit capability grants for files/data classes, sensors, IPC endpoints and networking;
- background authority separate from interactive authority;
- no root/system UID escape hatch;
- app-to-app sharing requires explicit object/capability transfer;
- uninstall revokes durable app authority and removes or cryptographically discards private app state according to policy.

### P13.4 Boot and update

- signed boot chain rooted in target-specific trust anchor;
- rollback protection for system/kernel/security policy versions;
- authenticated A/B or equivalent transactional update design;
- interrupted update returns to a known verified slot/recovery state;
- recovery environment has a narrower authority set than normal OS and cannot silently extract protected user data;
- production debug/unlock state is explicit, user-visible and measured into security state where hardware supports it.

### P13.5 Cryptography and key hierarchy

- do not invent cryptographic primitives;
- use reviewed implementations of standardized primitives behind narrow provider interfaces;
- keep protocol/domain separation explicit;
- use CSPRNG state with defined boot seeding/reseed behavior;
- zeroize transient sensitive buffers where this is meaningful and compiler-verifiable;
- separate device-root, system-update, storage, application/service and communication key purposes;
- test nonce uniqueness, key separation, failure handling and corrupted-ciphertext behavior, not only successful round trips.

### P13.6 Driver and hardware boundary

- map MMIO only into the component requiring it;
- DMA-capable devices require an IOMMU/device-domain plan when target hardware supports it;
- peripheral firmware is an untrusted input unless authenticated/measured by the platform chain;
- malformed device events/descriptors must not create kernel memory authority;
- driver/service crash cannot leave reusable DMA or memory mappings live.

### P13.7 Duress, lock and recovery semantics

Duress behavior must be explicitly separated from ordinary authentication. It must not depend on ambiguous UI behavior or leave obvious persistent artifacts revealing that a duress profile exists.

Before implementation, define:

- which secrets/data domains are accessible under normal unlock versus duress state;
- whether duress action is destructive, decoy, restrictive, remote-signal, or a combination;
- how false activation and recovery are handled;
- what an offline forensic adversary can observe after activation;
- how rollback/backup restore interacts with duress state.

No duress feature is considered complete until its physical-forensics and coercion threat model is documented and adversarially tested.

### P13.8 Observability without surveillance

- security events are compact typed records, not arbitrary content dumps;
- secrets, message contents, clipboard contents, raw sensor data and private filenames are excluded by default;
- retention is bounded;
- export requires explicit user/admin authority according to product mode;
- crash reporting is opt-in or locally inspectable according to product policy, with sensitive-memory scrubbing before export.

## Security gates

P13 exit requires all of the following categories, with reproducible test evidence:

1. **Authority replay:** old process/address-space/capability/IPC/mapping authority fails after recycle.
2. **Privilege attenuation:** delegation never increases rights/context/purpose.
3. **Isolation:** malicious app/service cannot read or modify another compartment without explicit capability.
4. **Boot integrity:** tampered kernel/system/update metadata fails closed into authenticated recovery.
5. **Rollback:** older signed-but-vulnerable version is rejected once rollback state advances.
6. **Storage:** offline image cannot decrypt protected classes without required key release conditions.
7. **Update fault injection:** power loss at each transactional stage produces either old-good or new-good state.
8. **Parser fuzzing:** high-risk untrusted parsers meet defined crash/memory-safety thresholds.
9. **Driver teardown:** device/service failure removes mappings/DMA authority and cannot be inherited by restart.
10. **Privacy audit:** enumerate every persistent identifier/log/telemetry channel and justify necessity, consumer and retention.
11. **Production configuration:** debug keys/interfaces and development bypasses are absent or cryptographically gated.
12. **External review readiness:** architecture, threat model, cryptographic constructions and update/boot chain are documented sufficiently for independent review.

## Design prohibitions

- No home-grown encryption/hash/signature algorithms.
- No master `root` identity that bypasses capability checks.
- No security decision based solely on reusable PID/TID/ASID/fd values.
- No writable policy file whose modification grants authority before authenticity is established.
- No silent downgrade from hardware-backed security to software-only behavior in production.
- No mandatory cloud account/telemetry dependency for local device security.
- No recovery path that bypasses the normal key-release threat model merely for convenience.

## Research references to keep evaluating

The roadmap research set should continue comparing NIST mobile/security guidance, Android/AOSP verified boot and sandboxing, GrapheneOS exploit resistance/anti-persistence, Apple hardware-rooted boot/data protection, QNX-style service isolation, BlackBerry/Symbian/Tizen lifecycle lessons, UNIX/Linux mechanisms and modern hardware security. Each adopted concept must be rewritten in Cookie terms and reviewed against the capability/context architecture before implementation.
