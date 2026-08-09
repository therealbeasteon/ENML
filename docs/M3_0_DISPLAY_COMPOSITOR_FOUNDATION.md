# M3.0 — Display/Compositor Ownership Foundation

M3 begins ENML's visible phone UI stack. M3.0 deliberately starts below widgets and visual styling: it freezes the ownership, geometry, frame-ordering and trusted-surface rules that every later shell, application UI, secure prompt and accessibility layer depends on.

## Product shape

```text
application / shell / secure-ui process
             |
             | future typed compositor IPC
             v
       ENML compositor core
       /       |        \
 surface   scene/input   frame timing
 ownership    policy      metadata
             |
             v
      private Linux display backend
      (DRM/KMS/driver work is later)
```

Linux display devices, DRM/KMS objects and native descriptors are not application API.

## Trusted identity and surface ownership

A surface is owned by the exact trusted `PeerIdentity` that creates it:

```text
SurfaceId -> { PrincipalId, UserId, ProcessId }
```

The caller never supplies a trusted owner separately from the already-resolved runtime identity. A same-principal replacement process receives a fresh `ProcessId` and cannot manipulate surfaces left by the old process. `revoke_process()` removes all surfaces owned by a dead execution.

The compositor is fixed-capacity:

- 64 global surfaces;
- 8 surfaces per durable application principal;
- one application root per live process in the M3.0 phone model;
- no heap-backed surface registry.

The per-principal budget prevents one application from consuming the complete global scene. The one-root rule also prevents a background process from promoting itself simply by minting another root window; future multi-window/root creation must be an explicit trusted-shell policy extension.

## Surface roles and application-stack authority

M3.0 defines four roles:

1. `application`
2. `popup`
3. `system_chrome`
4. `secure_system`

Applications may create one application root and popups attached to that root when the parent is owned by the exact same process identity. They cannot self-assign system-chrome or secure-system roles.

`system_chrome` is restricted to the configured trusted shell principal. `secure_system` is restricted to a distinct secure-UI principal. The compositor requires those principals to be nonzero and distinct.

Global z authority is compositor-owned. Applications do not submit arbitrary global z values. Application roots and their popups form a stack group; a popup stays above its own root but below a higher application group. A background application's popup therefore cannot leap over the foreground application.

`activate_application()` can promote an application+popup group only when called by the trusted shell principal. An application cannot activate itself. System chrome remains above application groups and secure-system UI remains above both.

Destroying an application surface also destroys its directly-attached popup surfaces, preventing a popup from retaining stale visual/input meaning after its parent is gone.

## Geometry and safe insets

The display configuration includes:

- physical pixel size;
- top/right/bottom/left safe insets;
- refresh rate in millihertz;
- a compositor scheduling margin in nanoseconds.

Surface geometry is bounded to the display. Negative origins, zero-area rectangles and rectangles extending beyond the configured display are rejected.

Safe insets are carried as display semantics rather than encoded into a vendor-specific screen layout. Later UI/layout code can distinguish full-bleed viewing content from interaction-safe regions without forking application logic by device model.

## Frame submission metadata

M3.0 does not yet expose shared pixel-buffer handles. It freezes the bounded ownership/timing metadata first:

- monotonically increasing nonzero frame sequence per surface;
- three logical buffer slots maximum;
- at most eight damage rectangles;
- damage rectangles use surface-local coordinates and must remain inside the surface bounds;
- geometry changes invalidate the previously submitted frame geometry while preserving sequence history.

Replay/duplicate frame sequences, invalid buffer slots and out-of-bounds damage fail before changing compositor state.

The three-slot limit is a bounded protocol ceiling, not a command to permanently allocate three full-size buffers for every surface. M3.1 will define the actual shared-buffer capability lifecycle.

## Frame deadlines

The compositor derives the next presentation boundary from the configured refresh rate and reports a submission deadline by subtracting the configured compositor margin. The margin is product/backend policy; it is not hard-coded as a percentage of a frame.

This puts frame-deadline awareness into the architecture before rendering complexity grows, while avoiding a background scheduling thread in M3.0.

## Scene snapshots, capture policy and input routing

`scene_snapshot()` returns a bounded deterministic snapshot containing current surface descriptors and most-recent frame metadata.

`secure_system` entries are marked `capture_allowed=false`. This is the first provenance hook for later screenshot/screen-recording policy; M3.0 does not yet implement a screenshot service.

`hit_test()` walks the trusted scene from top to bottom and considers only surfaces that are visible, input-enabled, backed by a submitted frame, and geometrically containing the point. Secure system UI therefore wins input routing over overlapping lower-trust content, and application popup routing follows the trusted application-stack order rather than a caller-selected z value.

## Reference guidance

M3.0 uses the supplied references for principles, not vendor APIs or visual identity.

### BlackBerry UI guidelines

The reference notes emphasize starting with user goals/workflows, using predictable standard components, forgiving behavior, and planning localization/accessibility rather than adding them after screen design. M3.0 therefore keeps the compositor contract semantic and role-based instead of baking a particular visual style or widget toolkit into the low-level scene model.

### Samsung One UI guidance

The supplied guidance distinguishes viewing/content areas from frequent interaction areas, emphasizes reachability on large phones, safe margins, responsive form factors, semantic navigation/actions and accessibility. M3.0 carries safe insets as first-class display configuration and avoids device-specific app layout forks. Reachability and responsive layout remain higher-level M3 UI responsibilities.

### C++ reference

Stroustrup's emphasis on type-rich lightweight abstractions and resource-conscious systems programming guides `SurfaceId`, explicit geometry types and fixed-capacity compositor state. Ownership is represented directly rather than through untyped global integers or heap-backed maps.

### Linux / Bootlin material

The Linux references place hardware resource management and system calls below userspace abstractions. ENML therefore keeps DRM/KMS/fbdev/device nodes and driver details out of the public display model. Hardware display integration remains a private backend/BSP concern and should follow upstream-first kernel/driver practice.

## Explicit non-goals

M3.0 does not yet provide shared pixel-buffer or DMA-BUF capability transfer, a supervised `system.compositor` executable/public OSIDL service, DRM/KMS atomic modesetting, GPU acceleration, physical display hotplug/fold-state handling, shell widgets/navigation visuals, a full accessibility semantic tree, screenshot/screen-recording service, or keyboard/touch device ingestion.

Those pieces build on the ownership rules frozen here rather than bypassing them.

## Acceptance gate

`display_compositor_test` proves:

- invalid display/safe-inset policy is rejected;
- apps cannot forge system or secure roles;
- one application root exists per process at this stage;
- popup parent ownership is exact-process-bound;
- an app cannot self-promote its stack group;
- a background popup stays below a higher application group;
- trusted shell activation promotes the root+popup group together;
- cross-principal/cross-process surface mutation is denied;
- geometry, frame sequence, buffer-slot and damage bounds are enforced;
- secure surfaces are non-capturable and win overlapping hit tests;
- geometry changes invalidate stale frame geometry;
- process revocation removes the dead execution's surfaces;
- a fresh `ProcessId` cannot inherit old surface authority;
- per-principal surface quotas remain isolated;
- parent destruction revokes dependent popup surfaces;
- frame deadline math is deterministic.

The gate runs with GCC, Clang, ASan/UBSan and native AArch64.

## Next

M3.1 should add typed shared-buffer capabilities and the first supervised compositor service boundary. Buffer allocation/import must remain bounded, move-only, identity-owned and backend-private; public apps must not receive DRM/KMS object IDs or display device fds.
