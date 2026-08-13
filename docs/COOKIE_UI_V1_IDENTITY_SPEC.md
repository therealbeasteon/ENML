# Cookie UI v1 — Identity and Finish Specification

Status: canonical UI v1 contract for Cookie OS.

## Purpose

Cookie UI is not a synthesis skin and must not visually depend on another mobile OS. Research from iOS/Liquid Glass, One UI, Android, Windows/Fluent, Xiaomi HyperOS, OnePlus/OxygenOS, Symbian/S60/UIQ, BlackBerry, Tizen, QNX and the project UI/UX references is used only to extract durable human-factors, performance, accessibility and architecture lessons.

The visual identity must remain recognizable when wallpaper, blur, transparency, color, shadow and depth are disabled.

## Canonical identity

Cookie UI v1 is defined by six inseparable ideas:

1. **Quiet Spatial Hierarchy** — open fields, strong hierarchy, lower-reach weighting on compact phones, and intentional asymmetry rather than walls of uniform cards.
2. **Authored Contours** — Anchor, Sweep, Pebble, Frame and restricted Halo contours form a grammar. Halo is a trusted/focused accent, not a generic card shape.
3. **Living Objects** — Home surfaces represent semantic objects such as apps, conversations, contacts, documents, controls, media and trusted system state. The app grid is not the conceptual center of the OS.
4. **Tactile Continuity** — motion preserves origin, direction and velocity context; direct manipulation can interrupt or reverse motion without snapping.
5. **Trusted Geometry** — authentication, privacy, recovery and security attribution use platform-owned secure-plane composition and capture policy rather than cosmetic branding alone.
6. **Adaptive Composition** — device geometry changes columns, lanes, reach weighting and scene structure while preserving identity. Compact/tall phones, wide phones and foldables are recomposed, not uniformly scaled.

## System scene signatures

### Lock Screen

Trusted status + identity field + privacy-filtered notification field + reachable actions. Frame/Halo are reserved for trusted identity/security context. Personal wallpaper may remain visible only under the wallpaper privacy contract.

### Home

Field / Shelf / Dock. Anchor + Sweep is the primary signature. Home should feel spatially open and calm rather than like a repeated card dashboard. Living Tiles are semantic and responsive.

### Quick Controls

Lower-weighted control field on tall phones; larger devices may widen into more columns. Hinge devices split around the seam. Controls use Pebble accents inside a larger Sweep/Anchor composition rather than one giant glass sheet.

### Notifications

Flowing semantic stream with newest/high-priority content placed near the reachable region on compact phones. Expanded devices may use two columns. Privacy classification is independent from layout.

### Settings

Grouped information hierarchy using Frame + Pebble. Density is allowed, but repeated identical card walls are not the default presentation.

### App Surface

Application content remains semantically neutral. Apps receive Cookie system semantics and accessibility behavior but do not own system security chrome or secure-plane styling.

### App Switcher

Spatial handoff between tasks. Compact phones use a flowing stack, expanded devices a paired field, foldables a seam-split field. Task order is stable; the switcher must never become a random thumbnail grid.

## Motion character

Cookie motion uses four characters: handoff, reveal, settle and secure.

- **Handoff:** preserves spatial relationship when moving between Home, app, switcher and task context.
- **Reveal:** exposes new information from a meaningful origin rather than a generic fade/zoom.
- **Settle:** lightly resolves direct manipulation without theatrical bounce.
- **Secure:** restrained, deterministic motion for trusted UI; no playful overshoot.

Motion is always cancellable/interruptible for ordinary interaction. Reduced Motion removes large spatial travel while preserving state feedback. Haptics may reinforce motion but cannot be the only signal.

## Smoothness and stability contract

Priority order:

1. input latency;
2. frame continuity;
3. motion continuity;
4. material;
5. depth;
6. ambient decoration.

The scheduler is refresh-rate aware. At 60/90/120 Hz, optional work must be reduced before interaction deadlines are missed. Quality degradation is immediate; quality restoration is hysteretic and gradual after stable frames. During direct manipulation, local damage redraw is preferred over scene-wide repaint. Unchanged surfaces stay cached when security/privacy state permits.

## Materials and depth

Quiet Depth is optional rendering richness, not identity. Material exists to clarify hierarchy. Full-screen glass dependency is forbidden. Blur, backdrop filtering, parallax and ambient depth must disappear cleanly under reduced transparency, accessibility, thermal, GPU, battery or memory pressure.

## Icon system

Icons are vector-preferred, optically balanced, layered within bounded depth/parallax limits and renderer-owned at presentation time. Cookie does not force every app into one universal squircle/circle mask. Individual silhouettes are allowed within a shared optical field and depth discipline. Permanent launcher animation clocks are forbidden.

## Wallpaper Scenes

Preferred master long edge: 3840 px; minimum admitted long edge: 2560 px for full-quality system scenes. Scene types may be still, layered or procedural. The platform controls crop, contrast, lock privacy and optional depth. Wallpaper must leave subject-safe and content-safe regions and may not compromise text/control contrast.

Canonical wallpaper families: Quiet Fields, Cookie Aurora, Contour Studies, Material Landscapes and Dark Field. Assets must be original or properly licensed.

## Living Tiles

Living Tiles are Cookie's widget model and do not inherit Windows Live Tile geometry. A tile declares semantic content and supported presentations (compact, standard, wide, tall, expanded); Cookie owns final composition. Widgets are glanceable, focused and battery-bounded. Continuous arbitrary animation and secret remote refresh are forbidden.

## Accessibility

Accessibility is structural: scalable text/icons, minimum touch targets, high contrast, reduced motion, reduced transparency, screen-reader semantics, switch access, keyboard/focus navigation and non-color-only state communication. Geometry must remain valid under text scaling and accessibility preferences.

## Privacy and trusted UI

Secure-plane surfaces require platform/trusted-service ownership, trusted attribution and capture denial. Ordinary apps cannot self-promote into secure UI. Locked previews obey public/private/secret classification. Secret objects cannot request remote enrichment.

## Forbidden resemblance patterns

Cookie UI v1 must reject:

- a full-screen translucent/glass sheet as the primary identity;
- a home screen dominated by repeated identical rounded cards;
- mandatory universal app-icon masks;
- unrestricted Halo use;
- secure-style motion outside trusted contexts;
- fixed layouts tied to one flagship handset ratio;
- decorative animation that blocks input or cannot be interrupted;
- widget-owned continuous frame clocks;
- identity that disappears when effects or wallpaper are disabled.

## Research-derived decisions

- Apple HIG: materials establish hierarchy; motion should be purposeful, brief, optional and cancellable; iPhone controls should account for hand reach and adaptive appearance/input.
- Samsung One UI: eye/hand ergonomics and comfortable reach are durable mobile principles.
- Android: adaptive layouts, bounded widget refresh and frame/jank discipline inform performance and responsive composition.
- Windows/Fluent: geometry, layout, motion, materials and widgets should operate as one system; motion reinforces spatial wayfinding and widgets remain glanceable/focused.
- Xiaomi HyperOS: graphics pipelines and system-wide animation tuning must be treated as platform engineering, while lock/home/widget adaptation spans device classes.
- OnePlus/OxygenOS: continuity under rapid app switching and long-run load is a useful smoothness target, but Cookie does not inherit its visual forms or bounce language.
- Symbian architecture: keep application engines/semantics decoupled from variant UI policy so Cookie can own a strong visual identity without contaminating app logic.

## Reference set

Official public references used for this v1 pass:

- Apple Human Interface Guidelines: Design Principles, Designing for iOS, Materials, Motion, Accessibility — developer.apple.com/design/human-interface-guidelines/
- Samsung Design: One Thought, One UI — design.samsung.com/global/contents/one-ui/
- Android Developers: adaptive layouts, performance/rendering, App Widgets — developer.android.com/
- Microsoft Windows App Design / Fluent — learn.microsoft.com/windows/apps/design/
- Xiaomi HyperOS — mi.com/global/hyperos and os1.hyperos.mi.com
- OnePlus OxygenOS 15/16 — oneplus.com/*/oxygenos15 and oneplus.com/*/oxygenos16
- Project-supplied Symbian OS Architecture Sourcebook, BlackBerry UI Guidelines and Figma/UI-UX references.

## v1 finish gates

Cookie UI v1 is considered architecturally finished when all of these hold:

- canonical scene grammar tests green;
- identity/adaptation tests green;
- wallpaper/widget privacy and quality tests green;
- secure-plane spoof/capture tests green;
- motion interruption and quality-hysteresis tests green;
- switcher composition and damage-aware redraw tests green;
- responsive geometry valid for compact, tall, regular, expanded and hinge profiles;
- renderer lowering preserves semantic identity at economy/balanced/full quality;
- final visual assets (icons, wallpapers, system glyphs, typography metrics) are original/licensed and pass accessibility/contrast review.

This file locks the design direction. Future research may improve implementation, but changing the six canonical identity ideas requires an explicit Cookie UI version change rather than ad-hoc styling drift.
