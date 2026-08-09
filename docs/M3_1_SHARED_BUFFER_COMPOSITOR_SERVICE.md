# M3.1 — Typed Shared Buffers and Supervised Compositor Service

M3.1 turns the M3.0 in-process compositor policy into a real supervised ENML platform service and introduces bounded shared pixel memory without exposing Linux graphics authority as public ABI.

## Security and ownership model

`SurfaceId` and `BufferId` are semantic service objects. Their authority is server-side and bound to trusted `PeerIdentity`; the numeric value is only a locator inside the current compositor generation.

Both identifier spaces are generation-scoped. The service receives the exact Supervisor generation through `BootstrapRecordV1.boot_generation` and composes new IDs inside that namespace. An application cannot choose or claim a generation. After `system.compositor` restarts, old channels remain dead and old semantic IDs cannot become authority merely because a fresh service allocates the same local slot number.

This closes the display-object ABA class that existed when every restarted compositor began allocating IDs from the same numeric sequence.

## Shared buffer model

The current host backend uses Linux memfd only as a private transport mechanism. Applications receive a move-only `SharedBufferLease` containing explicit ENML metadata plus the native shared-memory handle required to map pixels.

Current bounds:

- 48 live buffers globally
- eight live buffers per principal
- 24 MiB maximum per buffer
- 48 MiB maximum mapped buffer bytes per principal
- 128 MiB maximum mapped buffer bytes globally
- explicit pixel format, dimensions, stride and byte size
- CLOEXEC descriptor ownership
- fixed-size memfd with grow/shrink sealing

No DRM/KMS object ID, framebuffer device, render node, connector, CRTC, plane or Linux device path is part of the application contract.

## Frame binding

A frame submission names a compositor-owned `SurfaceId` and an authorized semantic `BufferId`. The compositor validates surface ownership, generation, sequence progression, damage bounds and buffer slot before accepting the frame.

Releasing/revoking a buffer invalidates any scene entry still presenting that buffer. Process revocation removes both surface and buffer authority for the dead execution.

## `system.compositor`

The product service:

1. adopts the private Supervisor control endpoint and public service endpoint;
2. receives trusted bootstrap identity, ServiceId and service generation;
3. constructs generation-scoped `Compositor` and `SharedBufferPool` state;
4. maintains a fixed `IdentityRegistry` populated by the Supervisor;
5. reports READY only after initialization succeeds;
6. polls the control and public endpoints without a hidden worker pool;
7. validates every public message against kernel-authenticated sender credentials before dispatch;
8. prunes dead client identities and their display resources;
9. exits cleanly when Supervisor/service peers disappear.

The service uses the existing M0/M2 process-identity substrate rather than inventing a display-specific identity model.

## Restart semantics

The M3.1 integration gate deliberately kills `system.compositor` and proves:

- the old main channel is permanently stale;
- the Supervisor starts a fresh service generation;
- the live client process retains its boot-scoped logical `PeerIdentity`;
- trusted identity publication is restored into the replacement service;
- a fresh service endpoint can be acquired;
- new `SurfaceId`/`BufferId` values are from the new generation namespace;
- old numeric display-object values cannot alias new authority.

A service restart therefore changes connectivity and generation-local object capabilities, not the application's process identity.

## Performance discipline

The first compositor service remains intentionally small and bounded:

- no general scene-graph scripting language;
- no application-supplied global z values;
- no unbounded surface or buffer lists;
- no hidden renderer thread pool;
- fixed damage metadata;
- explicit shared-buffer byte accounting;
- synchronous bounded control/RPC handling for this milestone.

Hardware-specific rendering/composition, DMA-BUF, DRM/KMS, GPU scheduling and display power management remain private later backend work and will be introduced only with measured need and target-hardware validation.

## Reference guidance

Stroustrup's resource-management guidance supports move-only RAII ownership for shared-memory handles and typed semantic identifiers. Linux/Bootlin material supports keeping kernel/driver details private and validating ARM64 natively. BlackBerry/One UI/mobile-UX references support predictable higher-level components, responsive layouts and accessibility while remaining separate from the compositor ABI.

The supplied *Android UI Design Basics* source further supports a bounded hierarchical semantic UI layer, reusable standard controls, responsive recomposition, separation of structure from themes/styles, event/callback semantics and minimizing unnecessarily complex view hierarchies. Those lessons are recorded for M3.2; Android Activity/Fragment/XML/resource APIs are not copied into ENML.

## Verification gate

M3.1 is required to pass:

- GCC Debug display gates
- Clang Debug display gates
- ASan/UBSan display gates
- native AArch64 display gates
- inherited M0, M1 and M2 workflow gates

The restart/generation integration test is included in the focused M3 label on every architecture above.
