# M3 Status

## M3.0 — display/compositor ownership foundation

Status: complete and merged.

Implemented:

- additive stable `ErrorDomain::display`
- no-exceptions/no-RTTI `core/osdisplay` library
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

## M3.1 — typed shared buffers + supervised compositor service

Status: implementation complete on PR #25; merge is gated on the final branch head remaining green.

Implemented:

- strong generation-scoped `BufferId` and `SurfaceId` namespaces
- generation bits derive from trusted Supervisor bootstrap state, never from application payloads
- stale semantic display IDs cannot alias a replacement compositor generation
- fixed 48-buffer global table and eight-buffer per-principal limit
- 24 MiB per-buffer, 48 MiB per-principal and 128 MiB global byte ceilings
- Linux memfd backing with CLOEXEC, exact size and grow/shrink/write seal policy
- move-only `SharedBufferLease` ownership
- exact `PeerIdentity` buffer ownership and process revocation
- frame submissions bind compositor-owned surface authority to semantic `BufferId`
- buffer release/revocation invalidates scene entries still presenting that buffer
- real supervised `system.compositor` service
- trusted identity publication through the existing pidfd-backed `IdentityRegistry`
- per-message `SCM_CREDENTIALS` validation before public compositor operations
- typed create-surface, allocate-buffer and frame-submit service operations
- exact Supervisor generation placed into `BootstrapRecordV1.boot_generation`
- restart integration proving old service channels stay dead, fresh endpoint reacquisition works, live application `PeerIdentity` remains unchanged, and old SurfaceId/BufferId values do not collide with new-generation objects
- M3 display CI builds/runs focused gates on GCC, Clang, ASan/UBSan and native AArch64

See `docs/M3_1_SHARED_BUFFER_COMPOSITOR_SERVICE.md` and the M3.1 PR.

## Next: M3.2 — bounded semantic UI tree + accessibility foundation

The next slice should:

- add a bounded hierarchical semantic UI tree above compositor surfaces and pixel buffers;
- define a small initial standard-role vocabulary (container, text, image, button, toggle, text field, list/collection item) without freezing vendor visual identity;
- carry explicit enabled/focused/selected/checked/pressed/visible state and typed actions separately from pixels;
- define logical/density-independent layout units from trusted display metrics and safe insets;
- support simple responsive recomposition, including one-pane vs two-pane list/detail structure from the same semantic content;
- introduce theme/design-system tokens separately from tree structure so typography/spacing/color/shape can evolve without changing semantic ABI;
- bound tree depth, node count, children per node, text/resource metadata and traversal work;
- add a platform-owned accessibility projection from semantic nodes rather than attempting to infer accessibility from compositor pixels;
- introduce deterministic focus/event dispatch semantics without opening direct input-device authority to applications;
- establish collection virtualization/recycling rules so large lists do not require unbounded live render nodes;
- prohibit blocking storage/network/media work from layout/render/event-dispatch hot paths;
- keep compositor, input router, semantic UI framework and shell/system UI as distinct responsibilities;
- validate with GCC, Clang, sanitizers and native AArch64 before adding complex animation or hardware graphics backends.

Reference guidance for M3.2 is recorded in `docs/REFERENCE_NOTES_2026_08_09_UI.md` and `docs/REFERENCE_ANDROID_UI_DESIGN.md`. Android-specific Activities/Fragments/XML/resource conventions are examples only and are not ENML ABI.
