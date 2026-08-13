# Cookie OS

Cookie is a phone operating system built for a compact trusted computing base,
explicit subsystem ownership, security and privacy by default, low resource
consumption, fast startup and resume, minimal background activity, power
efficiency, hardware portability and an original accessible mobile UX.

The project has three names and they are not interchangeable:

| Name | What it is |
| --- | --- |
| **Cookie** | The operating system. The product. |
| **Cookie Kernel** | The microkernel Cookie runs on. |
| **EMNL** | The security architecture — the boundaries, policies and invariants Cookie enforces. |

See `docs/NAMING.md` for why the distinction is maintained, and why the
repository is still called ENML. Source identifiers (`os::` namespaces, `emnl_*`
CMake targets) have deliberately not been renamed; that is a change to be made
on its own, verified on its own.

Historical and contemporary OS/security/UI references are engineering guidance,
never implementations or visual templates to copy. "Symbian-like" means
compactness, modular ownership and event-driven resource discipline — not
Symbian ABI compatibility or a recreation of its UI. `docs/PROJECT_VISION.md`
holds the full reference policy.

## Where the project is

| Layer | State |
| --- | --- |
| Build, IPC, OSIDL, supervisor, sandbox (M0) | Complete, frozen gate |
| Package and application lifecycle (M1) | Complete |
| Storage, keys, service broker, runtime session (M2) | Complete |
| Display, compositor, semantic UI and accessibility (M3) | Complete |
| Trusted phone shell and product security (M4) | Through M4.10g merged, remainder in flight |
| Verified boot evidence (M5) | Designed and tested; no platform produces the evidence yet |
| Device access policy, time protection (M6) | Policy complete; no platform enforces it yet |
| Cookie Kernel (M7) | Through M7.5a merged — ABI, host-testable core, first real AArch64 machine operations |

`docs/ROADMAP.md` is the plan of record from here to a shippable device.

**A caution about "merged".** M4.10h, M7.5b and M7.5c report as merged on GitHub
but are absent from `main`. Each was merged into a stack parent that had already
been merged to `main` moments earlier, so the content never arrived. Their
commits survive in the open stack branches and are recovered by landing those
stacks. Check ancestry rather than PR state.

Three statements that belong with any claim about Cookie:

1. **Nothing has run on physical hardware.** Every property is established on
   host or emulated builds.
2. **The kernel is not yet the substrate.** Linux is still underneath the service
   layer; M7 has built the Cookie Kernel beside the system, not under it.
3. **The security architecture is further along than the system it secures.**
   EMNL's identities, capabilities, wire formats and policies are mature. The
   hardware mechanisms that make them enforceable are not built.

## The kernel decision

Cookie is acquiring its own microkernel. Linux stops being the substrate and
becomes, at most, a development host. `docs/M7_0_KERNEL.md` records the decision
and `docs/SUBSTRATE.md` records the position it reverses.

The security case does not rest on being different from Linux — a new kernel
begins with its own undiscovered defects. It rests on being **small enough to
review completely**, and it is only redeemed if the kernel stays small. QNX
shipped an entire operating system in 15,930 lines on a 605-line kernel; that is
the class of artefact being aimed at. If Cookie Kernel reaches Linux's size, the
exercise has failed regardless of what else it achieved.

Four responsibilities go in the kernel: address spaces and threads, synchronous
message passing, interrupt dispatch, and scheduling. No filesystem, no drivers,
no network stack, no POSIX, no paging to storage.

## Architectural rules

Development preserves these invariants. `AGENTS.md` holds the complete and
authoritative list.

- Applications receive semantic, bounded APIs rather than raw privileged device
  interfaces.
- Identity and ownership are re-authorized at subsystem boundaries, never
  accepted from application payloads.
- Capacities, queues, shared-memory budgets and work scheduling are explicit and
  bounded.
- Stale generations, revisions and frames fail closed rather than aliasing
  replacement state.
- UI meaning and accessibility come from semantics, not framebuffer scraping.
- Work is event-driven and becomes quiet when there is nothing useful to do.
- Renderer internals — font files, glyph IDs, RGB mappings, GPU handles,
  compositor state — remain platform-private.
- Richer optical effects degrade without changing Cookie's semantic hierarchy or
  visual identity.
- Secret bytes are never compared with `==`, `memcmp` or `std::equal`; use
  `os::core::constant_time_equal` and wipe with `os::core::secure_zero`.

## Build and test

Host debug build:

```sh
cmake --preset host-debug
```

```sh
cmake --build --preset host-debug
```

```sh
ctest --preset host-debug
```

Sanitizers:

```sh
cmake --preset host-asan
```

```sh
cmake --build --preset host-asan
```

```sh
ctest --preset host-asan
```

Focused milestone gates, by CTest label:

```sh
ctest --preset host-debug --output-on-failure -L '^m3-ui$'
```

```sh
ctest --preset host-debug --output-on-failure -L '^m7-kernel$'
```

Twelve CI workflows cover GCC, Clang, sanitizers, cross-compiled and native
AArch64, resource budgets and nightly fuzzing. A PR head is not considered
validated because an older branch head was green.

## Repository map

- `core/oscore` — stable identities, results, errors, bounded primitives, constant-time and secure-zero helpers
- `core/oskernel` — Cookie Kernel: ABI, capabilities, scheduler, interrupt and machine layers
- `core/osipc` — local IPC transport and RPC foundations
- `core/osboot` — boot state, attestation, sealing, transparency, profile protectors
- `core/oskeys` — key service, hierarchy, providers, persistence
- `core/osstorage` — descriptor-rooted private storage
- `core/ospkg` — package identity, manifests, staging and activation
- `core/ossandbox` — process confinement
- `core/osservice` — service lifecycle primitives
- `core/osdevice` — device access policy
- `core/ostime` — time and partition accounting
- `core/osdisplay` — surfaces, buffers, compositor authority, trusted presentation and input seams
- `core/osui` — semantic UI, accessibility, layout, collections, design tokens, text, input, motion, CPU raster
- `core/osaccessibility`, `core/oscollection` — accessibility and collection transports
- `core/osapp`, `core/osshell` — application and trusted shell surfaces
- `interfaces` — OSIDL definitions and service interfaces
- `system/services` — supervised system-service implementations
- `system/supervisor` — service lifecycle, bootstrap and runtime identity authority
- `system/app_manager` — application lifecycle and sandbox ownership
- `tests` — unit, integration, adversarial, budget and architecture-specific validation
- `fuzz` — fuzz targets for wire formats and parsers
- `tools` — OSIDL compiler and build tooling
- `docs` — milestone contracts, threat and architecture notes, project vision, naming, roadmap

## OSIDL

The repository includes a deliberately small build-time OSIDL compiler. The
encoding remains pre-freeze; new public interfaces should be frozen only after
their in-process security and ownership semantics have stabilized.

Example generation after a host build:

```sh
./build/host-debug/tools/osidlc/osidlc --input interfaces/echo/echo.osidl --out-dir /tmp/cookie-echo-generated
```

## Project direction

The canonical product charter is `docs/PROJECT_VISION.md`. The plan of record is
`docs/ROADMAP.md`. Reference-use guardrails live in
`docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md` and
`docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md`.

The intended UI character is original to Cookie: classic, crafted and luxurious,
colorful, dimensional and curve-authored, with controlled translucency and
meaningful motion. It must stay legible, responsive and recognizably Cookie when
accessibility, capability, thermal, power or frame-budget constraints reduce
premium effects.

## License

See `LICENSE`.
