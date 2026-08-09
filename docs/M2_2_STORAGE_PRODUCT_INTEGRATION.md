# M2.2 Storage Product Integration

M2.2 moves private application data behind the real Storage Service in the product launch path.

## Authority flow

```text
Package/App trusted state
        |
        | ApplicationIdentity + UserId
        v
ApplicationPrincipalStore
        |
        | durable PrincipalId
        v
App Manager -----------------------------+
        |                                 |
        | private supervisor control      | retained authorized data-root fd
        v                                 |
  system.storage <------------------------+
        |
        | public app service endpoint
        v
launched application
        |
        | open_private_root()
        v
RequestContext.peer -> (PrincipalId, UserId) -> PrivateRootRegistry
        |
        v
DirectoryObjectHandle / FileObjectHandle
```

The application never supplies the identity used to select its root. Its public `open_private_root()` request contains no PackageId, PrincipalId, UserId, native PID, uid/gid, fd number or Linux path.

## Application bootstrap cutover

Linux-private fd 5 used to carry the authorized private-data directory into the application process. M2.2 replaces that authority with a connected Storage Service endpoint.

`application_storage_service_fd` remains an implementation detail. The test fixture explicitly `fstat()`s it and requires a socket before adopting it as an OSIP `Channel`.

The application sandbox is constructed with `private_data_directory_fd = -1`, so Landlock grants no direct private-data writable tree. Executable/runtime access remains separate from data authority.

## Trusted root publication

App Manager retains an already-authorized private-data directory for each installed `(ApplicationIdentity, UserId)` profile. It resolves the durable application PrincipalId from `ApplicationPrincipalStore` and publishes:

```text
(PrincipalId, UserId, authorized directory handle)
```

to `system.storage` over the private supervisor control channel.

This protocol is not exposed on the public Storage service endpoint. Carrying the target identity in this control payload is acceptable only because the endpoint itself is private system authority. Moving the same operation to public app RPC would violate the identity model.

## Launch ordering

A child is forked with bootstrap and Storage transport descriptors, but it first blocks waiting for the application bootstrap request. The parent registers the child process with the supervisor identity registry before sending that request. Therefore the application cannot successfully use Storage before its kernel sender credentials map to the intended `PeerIdentity`.

## Restart behavior

Storage object endpoints are generation-bound bearer capabilities.

When `system.storage` dies:

- existing directory/file object endpoints fail with `peer_died`;
- they are never silently reconnected to the next service generation;
- the restarted service begins with a fresh private-root registry;
- App Manager notices the new supervisor generation and republishes retained profiles;
- clients reconnect to the main Storage service and reacquire typed object capabilities;
- the durable PrincipalId/UserId remains the storage ownership key while each new process receives a fresh ProcessId.

This makes stale capability behavior explicit and prevents a dead endpoint from accidentally regaining authority after restart.

## CI boundary

M2.2 includes native process-supervision tests. These are valid on x86-64 Linux and native AArch64 but are not assumed safe under qemu-user.

The M0 CI suite now selects tests carrying the explicit `m0` CTest label. Its cross/QEMU job additionally excludes the M0 tests that require native supervisor/Landlock behavior. Later M1/M2 tests therefore cannot make the frozen M0 ARM64 signal appear broken simply because they exercise kernel process semantics unavailable under qemu-user.

## Current acceptance evidence

The focused M2 workflow builds and runs Storage gates with GCC, Clang and native AArch64, including `storage_app_manager_restart_test`. The inherited M1 App Manager and M0 runtime gates remain enabled separately.

## Deferred to M2.3

M2.2 retains global fixed-capacity tables but does not yet define complete per-principal resource accounting. M2.3 must add per-principal object/I/O quotas and make policy revocation invalidate matching live object slots deterministically.

Key Service and encryption remain separate from Storage object authority.
