# Cookie UI Research Matrix

This document turns the reference set into constraints for **Cookie UI**. It is intentionally comparative: the goal is to learn from prior systems without importing any vendor's visual identity, component geometry, branded interaction, icons, naming, or screen composition.

The canonical visual direction remains `COOKIE_UI_IDENTITY_DIRECTION.md`: **Quiet Depth**.

## Non-derivative rule

A reference is useful only when it can be translated into a general principle or measurable requirement. Cookie must not reproduce recognizable vendor-specific layouts, control centers, launchers, task switchers, icon masks, materials, animations, typography, status bars, lock screens, menus, or navigation chrome.

Cookie should remain recognizably Cookie when blur is disabled, color is removed, motion is reduced, wallpaper changes, icons become monochrome, and the same application moves between phone and large-screen modes.

## Symbian / S60 / UIQ

### What the research shows

Symbian deliberately separated the underlying operating system and generic UI/application frameworks from the final manufacturer UI. The same OS could support visibly different S60, UIQ, and MOAP user interfaces while retaining common application logic and data models. Its Uikon framework supplied policy-neutral GUI/application foundations that a UI variant could turn into a policy-rich product UI.

S60 was explicitly designed around one-hand-operated phones and a consistent interaction style. Its component system emphasized predictable lists, menus, notes, tabs, navigation, and device-independent conventions. Avkon controls were intended to scale with resolution/orientation and to keep applications behaviorally consistent.

Earlier Symbian/Psion work also explored application-centric and document-centric models instead of assuming that navigating a raw filesystem should define the user's mental model.

### Cookie lessons

- **Separate semantic UI contracts from look-and-feel policy.** Application code should request roles such as primary action, navigation destination, secure prompt, list, selection, transient surface, or document action; Cookie's renderer decides the final contour/material/motion.
- **Make Cookie's identity replaceable internally without breaking application logic.** Visual policy belongs above stable UI semantics, not inside every application.
- **Optimize common phone workflows for one-hand use**, but adapt rather than preserve phone layouts on larger displays.
- **Prefer platform components for ordinary interaction** so focus order, input behavior, accessibility, resizing, security attribution, and motion remain consistent.
- **Treat files, documents, apps, people, messages, and actions as different semantic objects.** Do not make the filesystem the universal navigation model.
- **Keep navigation deterministic.** A user should be able to predict back/close/home behavior from context; gesture animation must not alter the navigation model.
- **Support multiple input modes without forking the product identity.** Touch, keyboard, pointer, stylus, switches, and assistive input should share semantics even when presentation differs.

### Do not copy

- S60 softkey bars or Options-menu hierarchy;
- Avkon chrome, list appearance, status panes, tabs, icons, fonts, keypad conventions, or screen geometry;
- UIQ pen-era menu/panel composition;
- Symbian-era low-resolution density or modal-dialog habits.

## BlackBerry

### Cookie lessons

- preserve strong information hierarchy and fast expert navigation;
- make keyboard/focus traversal first-class rather than a touchscreen afterthought;
- provide clear system attribution for sensitive communication/security state;
- support efficient command access without hiding the primary path behind gesture-only interaction.

### Do not copy

BlackBerry iconography, menu structures, trackball/trackpad conventions, legacy status bars, typography, or security branding.

## Android

### Cookie lessons

- separate application artwork from system presentation;
- define safe optical zones and monochrome representations for icons;
- give the platform control over launcher motion/parallax and notification overlays;
- expose responsive/adaptive layout semantics rather than fixed screen coordinates;
- preserve application portability while allowing system-owned privacy/security UI.

### Do not copy

Material component shapes, Android adaptive-icon masks, launcher layouts, Quick Settings, navigation bars, gesture visuals, or Pixel-specific identity.

## Samsung One UI

### Cookie lessons

- place high-frequency phone controls within comfortable reach;
- separate viewing space from interaction space when it improves one-hand operation;
- reduce competition around the user's current task;
- recompose layouts for different screen classes instead of merely scaling them.

### Do not copy

One UI panel geometry, Quick Settings, app layouts, iconography, typography, edge panels, or motion signatures.

## Apple iOS / Liquid Glass

### Cookie lessons

- material should communicate hierarchy and transient/persistent roles rather than exist as decoration;
- foreground content should remain visually primary;
- layered icon art can add depth while preserving recognition;
- reduced-transparency and reduced-motion modes must remain fully coherent;
- transitions should preserve object identity between launcher, app, and system surfaces when possible.

### Do not copy

iOS navigation bars, Control Center, Dynamic Island, home-screen composition, icon silhouettes, SF Symbols, typography, Liquid Glass styling, or Apple transition signatures.

## Microsoft Windows / Fluent

### Cookie lessons

- geometry, material, elevation, motion, navigation, typography, iconography, haptics, and widgets require one governing system;
- use different materials for persistent, transient, and modal roles;
- make windowing and responsive layout first-class;
- allow shared/tiled window boundaries to alter geometry while preserving a coherent shape family.

### Do not copy

Mica/Acrylic appearance, Fluent iconography, Windows corner language, taskbar/start menu, title bars, Snap UI, or desktop chrome.

## Xiaomi HyperOS

### Cookie lessons

- treat the home screen as an information surface, not only an icon grid;
- make widgets, icons, lock-screen composition, and personalization part of one system;
- dense layouts can remain usable when hierarchy and spacing are explicit;
- input latency belongs to visual/interaction quality, especially for direct touch and stylus manipulation.

### Do not copy

HyperOS icons, Control Center, lock-screen templates, widgets, launcher layout, animations, or branded personalization styles.

## OnePlus / OxygenOS

### Cookie lessons

- maintain continuity across app launch, task switching, lock screen, launcher, and large-screen multitasking;
- allow density and icon sizing to vary without breaking hit-target/accessibility metrics;
- judge smoothness over repeated interactions and long sessions, not isolated showcase transitions.

### Do not copy

Fluid Cloud, Open Canvas, Shelf, OnePlus icon geometry, launcher structure, blur/rounding treatment, or branded motion.

## Tizen

### Cookie lessons

- maintain clean separation between platform services, application model, UI framework, and device profile;
- treat different device classes as profiles over common semantics rather than unrelated products;
- keep power/resource budgets visible to UI/runtime policy.

### Do not copy

Tizen visual components, wearable ring/navigation patterns, iconography, or Samsung/Tizen product styling.

## QNX / embedded UI references

### Cookie lessons

- deterministic behavior and failure containment matter more than decorative sophistication;
- system UI must continue to expose critical state under resource pressure;
- renderer effects need graceful quality tiers and safe fallback paths.

### Do not copy

Automotive/industrial dashboard composition or vendor branding.

## Figma / supplied UI-UX references

### Cookie lessons

- design tokens must map to implementation semantics one-to-one;
- component variants should represent real state, input, size-class, accessibility, and security differences rather than arbitrary styling combinations;
- prototype flows should include error, loading, offline, permission, large-text, reduced-motion, and destructive-action states;
- spacing, type, motion, geometry, and icon rules need measurable definitions before high-fidelity screens multiply.

## Security and hardening references

The supplied OS/security, NIST, FIPS, Knox, GrapheneOS, MDM, mobile-network, platform-hardening, and duress references affect UI identity whenever trust decisions reach the user.

### Cookie lessons

- secure prompts and authentication belong to a renderer/system-owned **secure plane** that applications cannot counterfeit;
- sensitive capabilities need persistent, attributable indicators where appropriate;
- protected content must be able to opt out of screenshots, recording, mirroring, or untrusted overlays;
- app identity, requesting principal, data scope, persistence, and consequence should be visible at permission time;
- themes/personalization cannot change trusted attribution semantics;
- emergency/duress flows must prioritize reliability, ambiguity resistance, and safe failure over elaborate animation.

## Cookie-native synthesis

The research converges on a UI that should not be defined by a single fashionable material treatment. Cookie's identity comes from the combination below.

### 1. Trusted planes

`background -> content -> control -> transient -> secure`

Depth is semantic. Secure-plane rendering and attribution are reserved to the platform.

### 2. Authored contours

Cookie uses role-driven shape families instead of applying one rounded rectangle everywhere:

- **Anchor** — stable reading/content containers;
- **Sweep** — directional and one-hand-oriented controls/sheets;
- **Pebble** — compact actions and icon bodies;
- **Halo** — focus/authentication/biometric emphasis;
- **Frame** — windows and large responsive surfaces.

The exact geometry will be tokenized and renderer-owned.

### 3. Tactile motion

Motion roles are semantic:

- **touch** — immediate input acknowledgement;
- **settle** — bounded completion after manipulation;
- **travel** — spatial task/navigation transition;
- **reveal** — progressive disclosure;
- **secure** — deterministic trusted transition;
- **ambient** — optional, low-priority background movement.

Animations must be interruptible and reversible where the state model permits it. Input is never blocked merely to finish an animation.

### 4. Quiet color

Personalization may influence non-security accent roles. Security/privacy/error/focus semantics keep guaranteed contrast and cannot be wallpaper-controlled.

### 5. Privacy legibility

Camera, microphone, location, capture/recording, secure input, trusted prompts, and policy-mediated cross-app data movement use platform-owned attribution.

### 6. Semantic objects, not app chrome

Cookie Home and system search should understand semantic objects: application, conversation, contact, document, setting, device, action, and trusted system state. Results expose only information authorized for that surface.

### 7. One system across input and size classes

Phone, foldable, tablet, desktop-class window, keyboard, touch, pointer, stylus, and assistive technologies share semantic controls and state. Layout/presentation can recompose while behavior remains predictable.

## Initial design contracts

These names should guide implementation and Figma tokens; they are semantic, not pixel values.

### PlaneRole

- `Background`
- `Content`
- `Control`
- `Transient`
- `Secure`

### ContourRole

- `Anchor`
- `Sweep`
- `Pebble`
- `Halo`
- `Frame`

### MotionRole

- `Touch`
- `Settle`
- `Travel`
- `Reveal`
- `Secure`
- `Ambient`

### QualityTier

- `Essential` — legibility, input response, focus, security attribution;
- `Continuity` — navigation/task motion and stable compositing;
- `Material` — translucency/blur/material response;
- `Depth` — parallax, lighting, extra layered effects;
- `Ambient` — optional decorative movement.

Resource pressure must degrade from `Ambient` toward `Essential`, never the reverse.

### InputMode

- `Touch`
- `Pointer`
- `Keyboard`
- `Stylus`
- `Switch`
- `Assistive`

These should influence focus/hover/target presentation without forcing applications to implement separate business logic.

## Design review gate

A proposed Cookie screen fails review if:

1. it is recognizable primarily because it resembles another vendor;
2. removing blur/color destroys its identity or hierarchy;
3. security attribution can be reproduced by a normal app;
4. touch, keyboard, or assistive focus leads to incompatible action ordering;
5. large-screen mode is just a scaled phone UI;
6. animation completion is required before input can proceed;
7. personalization can reduce legibility or counterfeit trusted state;
8. application logic depends directly on renderer-specific paint/material values;
9. ordinary controls bypass platform semantics without a demonstrated need;
10. a component cannot explain its purpose without referring to a vendor reference.

## Research sources used

The working corpus includes the user's supplied Symbian architecture sourcebook and other uploaded OS/UI/security references, plus public S60/Symbian documentation and the previously reviewed Android, One UI, Apple, Windows/Fluent, HyperOS, OxygenOS/OnePlus, Tizen, QNX, Figma/UX, security, and hardening sources.

Symbian-specific public references consulted include the Nokia Series 60 UI Style Guide, S60 UI Style Guide, and S60 Avkon control documentation. These are used for historical interaction/architecture lessons only.