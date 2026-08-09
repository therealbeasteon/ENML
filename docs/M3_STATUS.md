# M3 Status

## M3.0 — display/compositor ownership foundation

Status: implementation complete on the M3.0 branch; merge is gated on final branch-wide CI.

Implemented:

- additive stable `ErrorDomain::display`
- new no-exceptions/no-RTTI `core/osdisplay` library
- strong 64-bit `SurfaceId`
- bounded display size, safe insets, refresh timing and compositor scheduling margin
- four trusted surface roles: application, popup, system chrome, secure system
- exact `PeerIdentity` surface ownership
- distinct trusted shell and secure-UI principals
- one application root per live process in the initial phone model
- popup attachment restricted to an exact-process-owned application parent
- compositor-owned application stack groups; a popup cannot escape above its parent application group
- trusted-shell-only `activate_application()`; applications cannot self-promote or submit arbitrary global z values
- system chrome above app groups and secure-system UI above both
- 64-surface global table and eight-surface per-principal budget
- process-death surface revocation with no authority inheritance across fresh `ProcessId`
- bounded three-slot frame metadata and eight damage rectangles
- monotonically increasing frame sequence and replay rejection
- geometry mutation invalidates stale frame geometry
- frame timing/deadline calculation from display refresh policy
- deterministic bounded scene snapshot
- secure-system surfaces marked non-capturable
- trusted top-down hit testing over visible/input-enabled/framed surfaces
- GCC, Clang, ASan/UBSan and native AArch64 M3 display gates

See `docs/M3_0_DISPLAY_COMPOSITOR_FOUNDATION.md`.

M3.0 deliberately does not expose DRM/KMS/fbdev/device nodes, raw global z values, GPU APIs, or pixel-buffer descriptors as public application ABI.

## Next: M3.1 — typed shared buffers + supervised compositor service

The next slice should:

- add bounded move-only shared pixel-buffer capabilities with explicit format/stride/size metadata;
- keep allocation/import authority inside the compositor/backend rather than letting apps choose DRM/KMS objects;
- enforce per-principal and global buffer-byte budgets;
- make submitted frame slots refer to compositor-authorized buffer capabilities rather than metadata-only slots;
- add a real supervised `system.compositor` service using the existing `ProcessAuthority` / `ServiceBroker` identity model;
- derive every public surface operation from trusted `RequestContext.peer`;
- preserve secure-system role assignment as private trusted policy, not an app request claim;
- prove stale buffer/surface endpoints after compositor restart and fresh reacquisition through the M2.10 runtime-service session;
- keep frame work bounded and avoid hidden background renderer threads until the backend requires reviewed concurrency;
- validate under GCC, Clang, sanitizers and native AArch64 before hardware-specific DRM/KMS work.
