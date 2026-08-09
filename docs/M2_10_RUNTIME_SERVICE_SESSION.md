# M2.10 — Runtime Platform-Service Session

M2.9 gave one launched application a single boot-scoped `PeerIdentity` across multiple supervised services and transferred its initial Storage/Key endpoints through bootstrap v2. M2.10 makes that application/service relationship survive an individual system-service crash without weakening stale-capability semantics.

## Product shape

```text
ApplicationManager
      |
      | bootstrap v2 / READY
      | then long-lived private session on the same channel
      v
application fd 3
      |
      +-- Acquire(Storage Service)
      +-- Acquire(Key Service)
             |
             v
       ServiceBroker
       /          \
Storage Supervisor  Key Supervisor
       |                 |
 current generation   current generation
```

The runtime session is not a general-purpose bus. It only reacquires services already granted to that application at bootstrap.

## Security invariants

- The application cannot select a daemon executable, native descriptor number, `PrincipalId`, `UserId`, or logical `ProcessId`.
- Every runtime request carries kernel-supplied `SCM_CREDENTIALS`; App Manager compares those exact credentials with the broker-owned boot-scoped process record before returning a service endpoint.
- The bootstrap-v2 ServiceId set is the application-visible allow-list. A ServiceId outside that set receives `access_denied` even if the broker knows the service globally.
- `ServiceBroker` independently requires the process to remain attached to the requested service.
- Service generation is observation metadata, not authority. `known_generation` is advisory; trusted Supervisor state selects the endpoint that is returned.
- A service restart never mutates an old channel or object capability into a new generation. Old channels stay stale and fail with `peer_died`; reacquisition mints a distinct new main-service capability.
- Reacquisition does not mint a new process identity. Storage, Keys, App Manager, the broker and `ProcessAuthority` continue to use the same `PeerIdentity` for the live application process.
- App Manager reconciles generation-local Storage/Key policy before servicing runtime reacquisition, so a client cannot receive a connection to a replacement service before desired profile/key policy has been republished.
- Uninstall closes the runtime session before broker identity revocation and process teardown. A still-running application cannot reacquire service authority after uninstall commits.
- App Manager processes at most four runtime-session packets per application per `maintain()` iteration. The session adds no polling thread, background worker pool or unbounded queue.

## Wire contract

The private runtime protocol uses service id `0x0000F011`, operation `Acquire=1`, and a fixed 16-byte little-endian payload:

```text
u16 version = 1
u16 payload_size = 16
u32 ServiceId
u64 known_generation
```

A successful response returns the same payload shape with the trusted current generation and exactly one `SCM_RIGHTS` endpoint. RPC error responses transfer no handles.

The protocol is private application/runtime control, not stable third-party public ABI at this milestone.

## End-to-end gate

The M2.10 application fixture:

1. receives Storage + Key endpoints through bootstrap v2;
2. creates private Storage state and a durable application key;
3. enters READY and transitions fd 3 into the runtime session;
4. proves a non-bootstrapped ServiceId is denied;
5. observes the current Storage and Key generations;
6. keeps using the original KeyObject until `system.keys` is killed and the old object returns `peer_died`;
7. reacquires a fresh Key Service endpoint, verifies the generation changed, reopens the same durable `KeyId`, and decrypts ciphertext created before the crash;
8. keeps using the original Storage root until `system.storage` is killed and the old object returns `peer_died`;
9. reacquires a fresh Storage endpoint, verifies the generation changed, opens a new private root, and writes a completion marker;
10. verifies the application `PeerIdentity` stayed identical in both Supervisors, the broker and `ProcessAuthority` across both restarts.

The gate runs under GCC, Clang, ASan/UBSan and native AArch64 together with the M2.9 broker tests.

## Reference guidance

The design uses the supplied references as evidence, not as APIs to copy.

- The Symbian Architecture Sourcebook describes capability-controlled Publish-and-Subscribe system state and conventional client/server system services. ENML borrows the principle that system state/resource ownership belongs to trusted OS components and that clients may observe/reconnect explicitly; it does not copy `RProperty`, Symbian session ABI, or System Starter internals.
- Symbian OS Internals treats IPC sessions, handles and publish/subscribe properties as protected kernel-managed objects. ENML keeps the analogous authority in typed channels, pidfd-backed process identity and server-held rights instead of application payload claims.
- Stroustrup's C++ guidance on type-rich lightweight abstractions, deterministic resource management and move semantics supports `PlatformServiceEndpoint`/`Channel` as move-only RAII capabilities instead of integer-fd public APIs.
- Bootlin/Linux material reinforces Linux as the hardware/process/resource layer with system calls beneath userspace. ENML therefore keeps Linux descriptors, credentials and Supervisor implementation private while presenting a narrow ENML runtime-service contract.
- The older C# event/subscription material is useful only conceptually: subscribers can observe changing state without owning the publisher. ENML does not add a CLR or general event runtime to implement this milestone.

## Explicit non-goals

M2.10 does not add:

- a DBus/systemd-style general service bus;
- caller-selected daemon names or executable paths;
- automatic mutation/rebinding of old object capabilities;
- service discovery outside the trusted bootstrap allow-list;
- a public application API for Linux PID/UID/GID or fd numbers;
- asynchronous reconnect threads or hidden background retry loops;
- telephony/modem discovery (that remains a later subsystem with its own hostile-device boundary).

## Next

With M2.10 complete, M2's storage/key/runtime-connectivity substrate is ready to close. The next product milestone should move into the display/compositor/UI track rather than further broadening the service broker. Hardware-rooted Key Service providers, verified boot/attestation and hardware monotonic anti-rollback remain later security/BSP integrations and must not be falsely claimed by the host OpenSSL provider.
