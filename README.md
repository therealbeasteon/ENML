# ENML OS

ENML OS is an incremental phone-OS project focused on a compact trusted computing base, explicit subsystem ownership, security by default, low resource consumption, fast response/startup, minimal unnecessary background activity, power efficiency, hardware portability and an original accessible mobile UX.

The project takes historical and contemporary OS/security/UI references as engineering guidance rather than as implementations or visual templates to copy. In particular, “Symbian-like” means compactness, modular ownership and event-driven resource discipline — not Symbian ABI compatibility or a recreation of its UI.

## Current milestone

Completed foundations include:

- M0: bounded core/IPC/OSIDL, supervised services, trusted runtime identity, Linux sandboxing and native AArch64 validation;
- M1: package/application foundation and lifecycle ownership;
- M2: key/private-storage/service-broker foundations;
- M3.0: compositor/surface ownership and trusted scene ordering;
- M3.1: typed shared buffers and supervised `system.compositor` service.

M3.2 is active on `m3-2-semantic-ui-foundation` / PR #26. It is building the bounded semantic UI, accessibility, collections, text, input and concrete renderer foundation above the compositor substrate. See `docs/M3_STATUS.md` and `docs/M3_2_EXIT_CRITERIA.md` for the exact implemented/current/remaining state.

The M3.2 branch already has real CPU-side ENML material/text pixels, not just UI mockups, but it deliberately does **not** claim a final production Unicode/font backend, final GPU renderer, final trust mark or final translucent material implementation yet.

## Architectural rules

ENML development is expected to preserve these invariants:

- applications receive semantic/bounded APIs rather than raw privileged Linux device interfaces;
- identity/ownership is re-authorized at subsystem boundaries rather than accepted from application payloads;
- capacities, queues, shared-memory budgets and work scheduling are explicit and bounded;
- stale generations/revisions/frames fail closed rather than aliasing replacement state;
- UI meaning/accessibility comes from semantics, not framebuffer scraping;
- motion/input/accessibility/collection work is event-driven and should become quiet when there is no useful work;
- renderer implementation details such as font files, glyph IDs, RGB mappings, GPU handles and compositor internals remain platform-private;
- richer optical effects must degrade without changing ENML's semantic hierarchy or original visual identity.

## Build and test

Host debug build:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Sanitizers:

```sh
cmake --preset host-asan
cmake --build --preset host-asan
ctest --preset host-asan
```

Focused M3 UI tests:

```sh
ctest --preset host-debug --output-on-failure -L '^m3-ui$'
```

Focused M3 display/compositor tests:

```sh
ctest --preset host-debug --output-on-failure -L '^m3-display$'
```

CI also exercises Clang and native AArch64 gates; the current PR head is not considered validated merely because an older branch head was green.

## Repository map

- `core/oscore` — stable core identities/results/errors and bounded primitives
- `core/osipc` — local IPC transport/RPC foundations
- `core/osdisplay` — surfaces, buffers, compositor authority, trusted presentation/input seams
- `core/osui` — semantic UI, accessibility, layout, collections, design tokens, text/input/motion and CPU raster foundations
- `interfaces` — OSIDL definitions/generation examples and service interfaces
- `system/services` — supervised system-service implementations
- `system/supervisor` — service lifecycle/bootstrap/runtime identity authority
- `system/app_manager` — application lifecycle/sandbox ownership
- `tests` — unit, integration, adversarial and architecture-specific validation
- `docs` — milestone contracts, threat/architecture notes, project vision and reference-use rules

## OSIDL

The repository includes a deliberately small build-time OSIDL compiler. The current encoding remains pre-freeze; new public interfaces should be frozen only after their in-process security/ownership semantics have stabilized.

Example generation after a host build:

```sh
./build/host-debug/tools/osidlc/osidlc \
  --input interfaces/echo/echo.osidl \
  --out-dir /tmp/enml-echo-generated
```

## Project direction

The canonical product charter is `docs/PROJECT_VISION.md`. The reference-use guardrails live in `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md` and `docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md`.

The intended UI character is original ENML: classic/crafted/luxurious, colorful, dimensional and curve-authored, with controlled translucency and meaningful motion. It must stay legible, responsive and recognizably ENML when accessibility, capability, thermal/power or frame-budget constraints reduce premium effects.
