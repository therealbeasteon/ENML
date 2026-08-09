# M2.1 — Storage Service and Typed Object Capabilities

M2.1 places the first real service boundary in front of the M2.0 descriptor-rooted private-storage runtime. The purpose of this milestone is not to expose Linux files or descriptors to applications. It is to prove that private storage can be reached through trusted caller identity and then represented as transferable, typed, rights-reduced OS objects.

## Trust path

The main Storage Service endpoint uses the normal OSIP RPC identity path:

```text
SCM_CREDENTIALS
    ↓
PeerIdentityResolver
    ↓
RequestContext.peer
    ↓
(PrincipalId, UserId)
    ↓
trusted private-root policy
    ↓
Directory capability
```

The request payload does not contain `PrincipalId`, `UserId`, `ProcessId`, Linux uid/gid, an fd number, or an absolute filesystem path. `ProcessId` is deliberately not part of private-data ownership: a restarted process for the same durable application principal and user must resolve to the same private data domain.

`Storage` uses service id `0x0000F020`. Object-capability traffic uses the private service id `0x0000F021`. `0x0000F010` remains reserved by the application bootstrap protocol; M2.1 explicitly separates these ids after detecting the collision during implementation.

## Root lookup

The M2.1 test/runtime policy implementation is `PrivateRootRegistry`. It is populated only by trusted code with an already-authorized `PrivateRoot`; there is no application registration API and no pathname-based registration entry point.

The registry key is:

```text
PrincipalId + UserId
```

not native PID, logical `ProcessId`, package-name text, or a caller claim.

The fixed registry is intentionally a bounded milestone implementation. A later integration step should replace or wrap it with a system root provider that can resolve installed application profiles without retaining an unbounded number of directory descriptors.

## Object capability representation

Opening the private root returns a dedicated local `SOCK_SEQPACKET` endpoint through `SCM_RIGHTS` together with semantic metadata:

```text
StorageObjectType::directory
rights = directory_rights::all
```

The native descriptor value itself is never serialized. The receiving runtime immediately wraps the transferred endpoint in a move-only `DirectoryObjectHandle` or `FileObjectHandle`.

Each server-side object slot contains:

- semantic object kind;
- authoritative rights mask;
- owning principal/user metadata for diagnostics/future policy;
- the underlying M2.0 `Directory`, `File`, or private-root reference;
- a dedicated object endpoint.

The dedicated endpoint is the bearer object capability. If every client copy is closed, the service observes peer death/hangup and reclaims the slot. Reusing an array slot creates a fresh socketpair, so a stale endpoint cannot address the new object merely because the numeric slot index is reused.

## Rights model

File rights are currently:

```text
read
write
stat
sync
```

Directory rights are currently:

```text
open_file_read
open_file_write
create_file
open_directory
create_directory
remove_file
remove_directory
atomic_replace
```

Client wrappers perform early checks for developer ergonomics, but those checks are not the authorization boundary. The server keeps the authoritative rights mask and checks every operation again.

Child-directory delegation is monotonic:

```text
requested_rights ⊆ parent_rights
```

Any attempt to request rights not present on the parent capability fails with `ErrorDomain::storage / invalid_rights`. The integration test bypasses the typed client wrapper and sends a raw valid OSIP request to prove the server itself rejects a rights-escalation attempt.

File capabilities derive their rights from the actual trusted open operation. A read-only file therefore does not become writable because a later caller claims a write bit.

## Bounded I/O

M2.1 keeps the synchronous milestone protocol inside the existing 64 KiB inline OSIP limit.

- one read/write operation is capped at 60 KiB;
- service-level atomic replacement is capped at 56 KiB;
- paths remain bounded by the M2.0 1024-byte `RelativePath` limit;
- transferred handles remain bounded by the global OSIP handle limit;
- server object slots and root-policy entries are fixed-capacity arrays.

Larger streaming/shared-buffer I/O is a later milestone. M2.1 does not create hidden worker pools or unbounded queues to simulate it.

## M2.0 confinement still applies

The service never reinterprets an application path as a global Linux path. Once a directory capability is selected, every path is still a validated `RelativePath` beneath that already-authorized object.

The M2.0 rules remain unchanged:

- no absolute paths;
- no `.` / `..` traversal;
- no empty path segments, NULs, or backslashes;
- segment-by-segment descriptor-relative resolution;
- `O_NOFOLLOW` on traversal/open;
- regular-file-only `File` objects;
- no public hard-link API;
- bounded same-directory atomic replace with file and parent fsync.

## Identity attack test

`storage_service_identity_test` forks an attacker after the trusted client has already obtained the connected main Storage Service endpoint. The attacker therefore possesses the exact same inherited socket endpoint as its parent.

It still cannot obtain the parent's private root:

```text
inherited endpoint
    + child sends request
    ↓
SCM_CREDENTIALS reports child PID
    ↓
PeerIdentityResolver rejects unknown child
    ↓
no root capability minted
```

The original parent can then successfully request its root on the same endpoint. This repeats the M0.7 rule at the storage authorization boundary: possession of a service connection is not possession of another process's identity.

Object endpoints are different by design. Once minted, they are bearer capabilities and may eventually be deliberately delegated. Delegation safety comes from object possession plus server-side rights reduction rather than re-deriving the original caller identity on every object operation.

## RPC handle transfer change

M2.1 generalizes normal RPC responses so a successful response may carry bounded `SCM_RIGHTS` handles. The wire header `HAS_HANDLES` bit and `handle_count` must exactly match the transferred descriptor count.

RPC error responses still forbid handles. A remote error carrying a handle is a protocol violation.

`InboundMessage::take_handle()` is an infrastructure ownership-transfer primitive. It moves one received `NativeHandle` out of the inbound message; typed higher-level APIs must immediately wrap it rather than expose descriptor integers.

## Verification gate

The focused M2 CI builds and runs on GCC, Clang, and native AArch64:

- `storage_private_storage_test`
- `storage_service_integration_test`
- `storage_service_identity_test`

The integration gate covers:

- identity-rooted private-root minting;
- directory creation and atomic replacement through the service boundary;
- file open/read/size through a transferred typed object endpoint;
- a read-only file capability lacks write authority;
- child-directory rights reduction;
- raw rights-escalation rejection at the server;
- inherited main-channel caller-identity rejection;
- continued service operation after the rejected identity attempt.

The inherited M0/M1 workflows continue to build the new code under Debug, Clang, ASan/UBSan, native AArch64, and the AArch64 cross/QEMU gates where applicable.

## Deliberate limits

M2.1 is the object-capability core, not the final product integration. It does **not** yet provide:

- a supervisor-managed `system.storage` executable;
- a system-only protocol for App Manager/profile policy to publish or resolve private roots;
- removal of the current private-data bootstrap fd from M1 application launch;
- storage quotas/accounting by principal;
- large streaming/shared-buffer file I/O;
- asynchronous `Task<T>` file APIs;
- document/media brokers or cross-application sharing;
- encryption/key-management integration;
- backup/restore;
- persistent object-handle restoration across service restart.

The current M1 fd-5 private-data bootstrap remains Linux-private and must not be documented or exposed as stable application ABI. The next integration milestone should replace application use of that root descriptor with a Storage Service connection/object capability before the public app storage API is frozen.

## Reference alignment

The design intentionally follows principles reinforced by the supplied operating-system references rather than copying historical APIs: protected resources live behind OS services; process identity and protection are enforced at system boundaries; handles represent access to kernel/service objects; and data caging remains separate from capability/authorization policy. Symbian's client/server, objects/handles, process-level trust, capability model and data-caging separation are especially relevant design evidence for this slice. The general OS references likewise reinforce stable higher-level APIs over private kernel/system-call mechanisms.

## Next

M2.2 should perform the product integration cutover:

1. introduce a trusted private-root provider/control path between application profile state and Storage Service;
2. run Storage Service as a supervised system service with an explicit descriptor/resource budget;
3. hand applications a Storage Service connection or root capability rather than the private-data directory fd;
4. add per-principal storage accounting/quota enforcement;
5. define service-restart semantics for outstanding storage object capabilities.
