# M2.9 — Identity-Preserving Multi-Service Broker

M2.9 removes a structural limitation left visible by M2.8: the original Supervisor prototype allocated logical process identity inside each service supervisor. Registering one application independently with `system.storage` and `system.keys` would therefore create two different ENML `ProcessId` values for one native execution.

M2.9 makes process identity boot-scoped rather than service-scoped and introduces a bounded trusted service broker. A launched application can now receive Storage and Key Service endpoints while both services resolve exactly the same `PeerIdentity`.

## Product shape

```text
                     ProcessAuthority
                    /        |        \
                   /         |         \
          Storage Supervisor |   Key Supervisor
                 |            |          |
          system.storage      |     system.keys
                 \            |          /
                  \           |         /
                   +---- ServiceBroker --+
                              |
                         App Manager
                              |
                   bootstrap v2 / SCM_RIGHTS
                              |
                         application
                    /                     \
             Storage channel          Key channel
```

The broker is a trusted in-process system mechanism, not a public bus and not a caller-selected daemon router.

## One boot-scoped process identity

`ProcessAuthority` is a fixed-capacity pidfd-backed authority shared by participating Supervisors.

For one live native process:

```text
Linux PID/UID/GID + pidfd liveness
              |
              v
        ProcessAuthority
              |
              v
PeerIdentity { PrincipalId, UserId, ProcessId }
              |
       +------+------+
       |             |
 system.storage   system.keys
```

Exact reacquisition of the same live PID with the same durable principal/user returns the same `ProcessId`. Rebinding the live native process to a different principal/user is rejected.

The pidfd is the exact process-generation evidence. Numeric Linux PID reuse cannot recover a prior logical process identity. After explicit final release, a later trusted registration of a still-live process starts a fresh authorization epoch and receives a new logical `ProcessId`.

The authority is bounded to 128 records. Individual service Supervisors keep only their own fixed-capacity publication sets and receive duplicated pidfds over their existing private identity-control channels.

## Supervisor composition

The existing one-service lifecycle Supervisor remains intact. Legacy construction creates a private `ProcessAuthority`, preserving previous M0-M2.8 behavior.

M2.9 adds explicit shared construction:

```cpp
ProcessAuthority authority;
Supervisor storage{storage_config, authority};
Supervisor keys{key_config, authority};
```

A service restart is not an identity restart. The Supervisor republishes still-live external mappings from the shared authority into the fresh service generation. Old service channels remain stale; new channels are acquired explicitly.

## Trusted bounded ServiceBroker

`ServiceBroker` has fixed capacities of eight registered system services and 64 attached processes.

Trusted system construction registers a Supervisor under its real `ServiceId`. Registration is rejected when the supplied ID does not match the Supervisor descriptor or when the Supervisor uses a different `ProcessAuthority`.

A broker attach is a multi-service identity transaction:

1. validate the requested bounded, unique service set;
2. acquire one broker-owned authoritative process reference;
3. publish the exact same identity to every requested service;
4. if a later publication fails, revoke the publications added by that call in reverse order;
5. release the broker-owned base identity when rollback is complete.

The broker does not silently adopt an identity that another trusted subsystem published directly to a Supervisor. Doing so would let a later broker detach revoke authority the broker never owned.

If rollback itself cannot fully revoke an already-published identity, the broker retains its base authority reference rather than freeing the `ProcessId` for unrelated reuse.

`connect(ProcessId, ServiceId)` succeeds only for a process/service pair already attached by trusted lifecycle code. It returns a fresh service-generation channel from the registered Supervisor. Possession of the broker object does not change per-message service identity validation: services still resolve `SCM_CREDENTIALS` through their trusted identity registries.

## Application bootstrap v2

M2.2 introduced a fixed Storage endpoint at Linux fd 5. That was acceptable for the first vertical slice but does not scale into a platform-service ABI.

M2.9 keeps bootstrap fd 3 and the private executable fd 4 construction mechanism, but new multi-service applications inherit no fixed service-specific descriptor. App Manager closes fd 5 and every higher inherited descriptor before sandbox/exec.

After exec, App Manager transfers a bounded typed endpoint set over the existing bootstrap channel with `SCM_RIGHTS`.

Bootstrap-v2 payload:

```text
u16 version = 2
u16 payload_size
u16 service_count
u16 reserved = 0
u64 application_instance_id
u64 process_id
u64 principal_high
u64 principal_low
u64 user_id
u64 package_generation
repeat service_count:
    u32 service_id
    u32 reserved = 0
```

The corresponding native endpoints are ancillary handles, not serialized fd integers. `service_count` must match the handle count, is bounded to four, and contains unique nonzero `ServiceId` values.

The application consumes endpoints by `ServiceId`:

```cpp
auto storage = request.take_service_endpoint(storage_service_id);
auto keys = request.take_service_endpoint(key_service_id);
```

This deliberately separates logical platform-service identity from whatever numeric fd Linux chooses when receiving `SCM_RIGHTS`.

## App Manager launch transaction

The five-argument M2.9 App Manager constructor requires Storage Supervisor, Key Supervisor and ServiceBroker composition over one shared ProcessAuthority.

Launch order is:

```text
trusted package + profile state
          |
          v
publish Storage + Key lifecycle policy
          |
          v
fork application
          |
          v
broker attach child to {Storage, Keys}
          |
          v
same PeerIdentity published to both services
          |
          v
broker obtains current generation channels
          |
          v
bootstrap-v2 transfers typed endpoints
          |
          v
application proves Storage + Key operations
          |
          v
READY v2
```

If identity attachment, channel acquisition, bootstrap send or READY validation fails, App Manager detaches the broker identity and kills/reaps the child. A failed multi-service launch therefore does not leave a half-authorized process in one service.

When an application exits, App Manager detaches its broker identity from every service. During manager teardown, identity is revoked before the process is killed so a still-open service fd does not preserve authorization after lifecycle revocation.

The legacy three-argument and four-argument App Manager modes remain for M1/M2 compatibility. Only explicit five-argument construction selects bootstrap v2 multi-service launch.

## Service generation semantics

A service crash closes its old endpoints. The old channel returns `peer_died` and is never mutated into a connection to the new generation.

Because process identity is held independently in `ProcessAuthority`, the restarted Supervisor republishes the same `PeerIdentity`. Trusted code may then call the broker again to acquire a fresh service-generation channel.

This preserves two distinct concepts:

```text
process identity generation != service process generation
```

Runtime delivery of a replacement endpoint to a long-running application can build on this explicit broker reacquisition primitive without changing the identity model.

## Reference-derived rationale

The supplied references are design evidence, not vendor APIs to copy.

### Symbian OS Architecture Sourcebook

Symbian repeatedly centralizes logical/physical resource ownership in system servers and describes System Starter as an explicit startup-policy mechanism used to control service sequencing and responsiveness. Its certificate/key architecture similarly provides single points of access rather than exposing backing implementations directly to applications.

M2.9 uses the same structural principle: process identity and service routing are centralized trusted system responsibilities, while applications receive narrow service capabilities. ENML does not copy Symbian descriptors, server ABI, capability names or historical implementation details.

### The C++ Programming Language

The C++ reference emphasizes RAII, deterministic resource ownership and preserving invariants rather than leaving partially-constructed state.

M2.9 applies that discipline to pidfds, channels and broker attachment. ProcessAuthority owns the authoritative pidfd; service publication receives duplicates; broker attach either establishes the requested service set or rolls newly-created authority back. Move-only `NativeHandle`/`Channel` values model ownership directly.

### Operating-system isolation references

The OS references emphasize process isolation, capabilities/protection boundaries and resource limits. M2.9 therefore keeps identity derived from kernel process evidence, preserves per-service credential verification, and bounds authority/service/process tables. The broker does not turn channel possession or a service-name payload into identity authority.

### BitLocker security-policy reference

BitLocker's useful architectural lesson for ENML remains separation between protected data keys and upstream protection/key-management state. M2.9 does not change that Key Service model; it only fixes how the same authenticated application execution reaches `system.keys` and `system.storage`. Historical BitLocker algorithms, recovery schemes and Windows APIs remain out of scope.

## Integration gates

`supervisor_shared_identity_test` proves two independently supervised services resolve the same native sender to one boot-scoped `PeerIdentity`, and that releasing one service publication does not destroy the identity while another service still references it.

`service_broker_test` proves:

- trusted service-directory registration;
- one identity across two distinct services;
- public child-service resolution of that identity;
- wrong-principal rebind rejection;
- duplicate/unknown service rejection;
- broker publication ownership;
- full detach/revocation;
- fresh ProcessId after explicit authorization-epoch end;
- transactional rollback when a later service is unavailable;
- old endpoint death across service restart;
- explicit broker reacquisition of a fresh service-generation channel with unchanged process identity.

`application_bootstrap_v2_test` proves bounded explicit serialization, typed ServiceId-to-handle association, CLOEXEC received descriptors, one-time move semantics, READY-v2 validation, duplicate-service rejection and service/handle-count consistency.

`app_manager_multi_service_broker_test` launches a real sandboxed application against supervised `system.storage` and host/CI `system.keys`. Before READY, the application performs both private Storage I/O and Key Service AEAD under the same broker-published identity. The parent verifies Storage Supervisor, Key Supervisor, broker and ProcessAuthority all report the same `PeerIdentity`, then verifies application exit removes authority from both services.

The dedicated `M2 Service Broker` workflow runs these gates under GCC, Clang, ASan/UBSan and native AArch64.

## Deliberately deferred

M2.9 provides the identity-preserving authority, trusted broker, explicit service-generation reacquisition primitive, and typed multi-service application bootstrap.

A later slice may add a long-lived application-side service-directory session for delivery of replacement endpoints after a service restart. It must reuse the same ProcessAuthority and broker rules rather than creating a second identity system.

Also unchanged/deferred:

- production TPM/TEE/HSM/secure-element key provider;
- verified-boot/attestation coupling;
- hardware monotonic anti-rollback for durable key state;
- general public service discovery;
- unbounded daemon registration or caller-selected executable routing.
