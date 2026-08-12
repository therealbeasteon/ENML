# Cookie UI — Native Visual Identity Direction

Cookie UI must look and feel native to Cookie OS. Android, iOS, Liquid Glass, One UI, Windows/Fluent, Xiaomi HyperOS, OxygenOS/OnePlus, BlackBerry, Symbian, Tizen, QNX, and the supplied UX/Figma references are design evidence only. They are not visual templates, compatibility targets, or permission to reproduce another platform's identity.

The working identity is **Quiet Depth**: calm, dimensional, precise, tactile, privacy-aware, and intentionally restrained.

## Research synthesis

### One UI

Useful lessons:
- prioritize the current task and reduce unnecessary visual competition;
- separate viewing space from high-frequency interaction space for one-handed reachability;
- preserve familiar context across devices;
- adapt composition to screen size rather than merely scaling controls;
- treat simplicity and reduced fatigue as product requirements.

Cookie must not copy Samsung panel geometry, iconography, typography, Quick Settings structure, or motion signatures.

### iOS / Liquid Glass

Useful lessons:
- use material to establish functional hierarchy, especially navigation and transient controls;
- keep content visually primary;
- let hardware and software geometry feel related without forcing every surface into one shape;
- use layered icon artwork to create depth while keeping the silhouette recognizable;
- provide reduced-transparency and reduced-motion fallbacks.

Cookie must not become a glass-themed iOS imitation. Blur, refraction, translucency, specular light, and depth are renderer techniques, not the identity itself.

### Windows / Fluent

Useful lessons:
- material, elevation, layout, iconography, typography, motion, navigation, haptics, and widgets should be governed by one coherent system;
- materials should have semantic roles: persistent base layers, transient layers, and modal focus should not all use the same optical treatment;
- geometry can vary with context while still following a consistent nested relationship;
- responsive windowing and large-screen layout should be first-class rather than phone UI stretched onto a desktop-sized surface.

Cookie should not inherit Mica/Acrylic names, Windows corner radii, taskbar patterns, Fluent icons, or desktop chrome.

### Xiaomi HyperOS

Useful lessons:
- the home screen is a configurable information surface, not merely an icon grid;
- widgets, icon systems, gallery/personalization, and lock-screen composition form one visual ecosystem;
- latency-sensitive stylus/touch interaction reinforces that UI identity includes input response, not just pixels;
- dense layouts can remain legible when hierarchy and spacing are deliberate.

Cookie should avoid copying HyperOS icon art, control-center layout, lock-screen templates, widget appearance, or branded animation signatures.

### OxygenOS / OnePlus

Useful lessons:
- motion should feel continuous across launcher, app opening, multitasking, lock screen, and system surfaces;
- scalable icons and flexible home-screen density improve personalization without requiring separate launchers;
- large-screen multitasking deserves a native spatial model;
- perceived smoothness must remain stable under repeated app switching and long sessions.

Cookie should avoid copying Fluid Cloud, Open Canvas, Shelf, OnePlus shapes, or the vendor's blur/rounding treatment.

### Android launcher / adaptive icon systems

Useful lessons:
- separate application artwork from final system presentation;
- preserve a safe optical region so icons remain recognizable across masks/effects;
- support a monochrome/system-color representation without forcing apps to maintain unrelated identities;
- keep launcher-provided parallax/motion under platform control rather than allowing every app to run arbitrary home-screen animation code.

Cookie should define its own icon container, contour, depth, monochrome, and motion contracts rather than inheriting Android adaptive-icon masks.

## Cookie identity: Quiet Depth

Five traits define the system.

### 1. Trusted planes

Cookie UI has semantic depth planes rather than arbitrary z-order decoration:

- **background plane** — wallpaper, ambient scene, non-interactive context;
- **content plane** — application content and primary reading surfaces;
- **control plane** — persistent navigation and direct controls;
- **transient plane** — menus, temporary panels, drag surfaces, alerts;
- **secure plane** — authentication, permission mediation, secure input, trusted system attribution.

Secure-plane appearance must be renderer-owned and unavailable to normal applications. Apps can request semantic actions, not paint a counterfeit secure prompt.

### 2. Authored contours

Cookie should use a family of swept/tensioned contours rather than generic rounded rectangles everywhere.

Recommended shape families:
- **Anchor** — low-curvature containers that visually stabilize a screen;
- **Sweep** — asymmetrical, one-hand-oriented controls and sheets;
- **Pebble** — compact icon/action surfaces;
- **Halo** — circular/elliptic focus or biometric/security affordances;
- **Frame** — larger windows/panels whose corners relax as they approach screen or split boundaries.

Shape should encode role. Maximized/tiled content may straighten at shared boundaries; floating/transient objects may use stronger contour expression.

### 3. Tactile motion

Cookie motion is continuous, interruptible, and state-preserving.

Rules:
- animation never blocks input simply to finish looking pretty;
- opening an app preserves the spatial relationship to its launcher icon/widget when practical;
- back/close motion reverses or resolves the same spatial path rather than inventing a new animation;
- gesture-driven surfaces track the user's finger with minimal phase lag;
- velocity and distance influence duration within bounded ranges;
- interrupted transitions resolve to a valid state without snapping through impossible geometry;
- reduced-motion mode keeps state feedback but removes large spatial travel.

Motion roles:
- **touch** — immediate press/drag response;
- **settle** — short spring/damped completion after direct manipulation;
- **travel** — navigation or task change with spatial continuity;
- **reveal** — progressive disclosure;
- **secure** — restrained, deterministic motion for trusted UI;
- **ambient** — optional low-frequency motion that must stop under power/privacy constraints.

### 4. Quiet color

Cookie should support expressive themes without letting wallpaper-derived color erase trust or readability.

- content can carry richer color;
- persistent controls use lower chroma unless state requires emphasis;
- secure-state colors are platform-owned semantic roles;
- selection/focus/error/privacy state never rely on color alone;
- wallpaper sampling may influence non-security accent roles but cannot change secure attribution colors or contrast requirements.

### 5. Privacy legibility

Security and privacy state are part of the visual identity, not notification clutter.

Platform-owned indicators should cover at least:
- camera use;
- microphone use;
- precise/coarse location use;
- screen capture/recording;
- secure input/authentication;
- sensitive clipboard or cross-app transfer where policy requires mediation;
- trusted system prompts;
- protected content that cannot be mirrored/captured.

The goal is quiet persistence: always attributable, hard to spoof, and visible without dominating the user's task.

## Native home screen: Cookie Home

Cookie Home should be a trusted system surface, not an interchangeable unprivileged launcher process with unrestricted access to application metadata.

### Layout model

Use a **field + shelf + dock** model:
- **Field** — free but grid-assisted placement for icons, widgets, folders, and trusted system tiles;
- **Shelf** — contextual/glanceable area whose contents are user-controlled and privacy-filtered;
- **Dock** — stable high-frequency actions/apps with strict one-hand reach targets.

Density may vary, but touch targets do not shrink below accessibility minimums. Visual icon size and hit target are separate metrics.

### Search and app discovery

Search should be local-first by default. Application discovery, settings, files, contacts, and actions should expose only metadata the caller is authorized to query. Remote suggestions require explicit user policy and must not make local launcher queries a telemetry feed.

### Widgets

Widgets are declarative snapshots/actions rendered through platform primitives. They should not receive arbitrary compositor access or permanent background execution just because they appear on the home screen.

### Folders and groups

Folders should support compact and expanded representations without creating hidden nested navigation mazes. Large folders may expose direct actions, but the system should preserve deterministic ordering and accessibility traversal.

## Cookie icon system

Cookie icons should have **three independent layers of identity**:

1. **Glyph** — the semantic mark supplied by an app/system component.
2. **Body** — Cookie's renderer-owned container/contour treatment.
3. **Depth** — optional bounded foreground/background separation and lighting response.

### Icon requirements

- apps provide vector-preferred artwork plus bounded raster fallbacks;
- a safe optical zone prevents critical glyph detail from touching the body boundary;
- icons must remain recognizable in full-color, dark, high-contrast, and monochrome modes;
- system can generate lighting/parallax from bounded depth metadata, not arbitrary application shaders;
- no icon animation can run continuously on the home screen without an explicit system animation role;
- notification badges are semantic overlays owned by the launcher, never burned into app artwork;
- security-sensitive system icons have reserved glyph namespaces and cannot be claimed by apps.

### 2D and 3D

Cookie is primarily a **2.5D** interface: 2D layouts with carefully bounded depth, lighting, parallax, and perspective. Full 3D should be reserved for content or specific spatial interactions.

Why:
- it provides depth cues without forcing every screen through a 3D scene graph;
- it keeps hit testing and accessibility deterministic;
- it preserves battery/GPU budgets;
- it allows graceful fallback to flat rendering with the same semantic layout.

No functional interaction may depend solely on perspective, lighting, or parallax.

## Windowing and multitasking

Cookie should have one spatial model that scales from phone to tablet/desktop-class displays.

Window states:
- focused full-screen;
- split/paired;
- floating task;
- transient companion;
- picture-in-picture/media surface;
- secure/system-owned overlay.

Large-screen layouts must recompose rather than scale. A two-pane app should become one coherent responsive scene, not two phone screenshots side by side.

Shared boundaries reduce contour radius/depth; detached windows regain stronger authored shape. This borrows the *principle* of geometry responding to spatial context without copying Windows/Samsung/OnePlus window chrome.

## Smoothness, optimization, and stabilization

Cookie smoothness is defined by **latency consistency**, not peak refresh-rate marketing.

### Frame contract

The compositor and UI runtime should track:
- input-to-photon latency;
- frame deadline misses;
- animation hitch clusters;
- layout/measure cost;
- GPU render cost;
- buffer acquisition stalls;
- app-start-to-first-interactive-frame;
- task-switch-to-interactive-frame;
- memory-pressure degradation.

### Rendering priorities

1. input response;
2. readable/stable content;
3. motion continuity;
4. material effects;
5. decorative depth/ambient effects.

When resources are constrained, degradation must occur from the bottom upward: drop expensive blur/specular/parallax before dropping interaction fidelity.

### Animation implementation

- transform/opacity/clip/material parameters should be compositor-accelerated where possible;
- avoid relayout on every animation frame unless the interaction semantically changes layout;
- animation graphs must have bounded node count and duration;
- offscreen and occluded animation pauses by default;
- background apps cannot retain unrestricted animation clocks;
- spring simulations use deterministic bounded parameters and terminate;
- animations must be reversible or safely interruptible;
- the renderer should support frame-rate-independent interpolation and variable-refresh displays.

### Stabilization gates

Before Cookie UI is considered production-ready:
- 60/90/120 Hz frame pacing tests where supported;
- repeated launcher↔app switching stress;
- rapid gesture reversal tests;
- orientation/fold/resize during animation;
- memory-pressure animation fallback;
- GPU-lost/device-reset recovery;
- long-duration idle/burn-in behavior for OLED;
- reduced-motion/transparency/high-contrast combinations;
- large-text reflow up to the platform maximum;
- screen-reader traversal during dynamic updates;
- screenshot/screen-record privacy enforcement;
- secure-plane spoofing tests.

## UX principles

Cookie UI follows these product rules:

1. **Purpose before ornament** — every visual effect has a state/hierarchy/motion reason.
2. **Directness** — common actions require short paths and immediate feedback.
3. **Recoverability** — destructive actions expose clear consequences and recovery where possible.
4. **Reachability** — frequent phone actions live inside practical thumb reach without wasting large-screen space.
5. **Continuity** — objects maintain identity as they move between home, app, task switcher, split view, and notifications.
6. **Attribution** — users can tell which app/service/system principal owns a surface or action.
7. **Privacy by default** — personalization and search work locally unless the user chooses otherwise.
8. **Accessibility is structural** — semantics, focus order, hit targets, text scaling, contrast, reduced motion/transparency are designed in, not patched on.
9. **Performance is UX** — missed deadlines, jank, input lag, and instability are design failures.
10. **Customization cannot counterfeit trust** — themes may change appearance but never secure attribution semantics.

## Development sequence

This design work can proceed in parallel with kernel milestones as long as it does not bypass roadmap gates.

1. Freeze semantic token vocabulary for planes, contours, motion, icon roles, privacy attribution, and quality tiers.
2. Define Cookie Home declarative model: field/shelf/dock, folders, widgets, search authorization, and app metadata privacy.
3. Define icon asset contract and renderer-owned body/depth pipeline.
4. Implement opaque 2D reference renderer first.
5. Add text shaping/measurement and large-text reflow.
6. Add compositor-aligned motion scheduler and frame telemetry.
7. Add bounded translucency/material effects.
8. Add 2.5D icon/surface depth under quality/power budgets.
9. Implement responsive windowing/multitasking states.
10. Add secure-plane visual attribution and spoofing tests.
11. Build Figma library using the exact same semantic token names.
12. Run accessibility, usability, smoothness, and long-session stabilization gates before freezing the final pixel identity.

## Non-derivative gate

A Cookie UI review fails if a screen can reasonably be described as "One UI with different colors," "Liquid Glass on Android," "Windows Fluent for phones," "HyperOS clone," or "OxygenOS with custom icons."

Cookie must remain recognizable when:
- blur is disabled;
- color is removed;
- animation is reduced;
- wallpaper changes;
- icons switch to monochrome;
- the UI moves from phone to tablet/windowed mode.

Recognition should come from the combination of trusted planes, authored contours, spatial composition, icon body language, tactile motion, privacy attribution, typography, and consistent behavior.