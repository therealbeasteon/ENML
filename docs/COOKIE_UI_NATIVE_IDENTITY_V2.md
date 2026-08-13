# Cookie UI Native Identity v2

Status: design lock candidate for Cookie UI v1 visual divergence pass.

## Why this replaces the previous launcher concept

The earlier Anchor Field / Contour Stream / Focus Orbit direction was intentionally non-derivative, but it asked the launcher to carry too much novelty at once. Cookie UI needs to be immediately distinctive while remaining calm, learnable, fast, accessible, and dependable on a phone used all day.

The revised direction keeps the strongest architectural work already completed—semantic objects, trusted planes, adaptive geometry, bounded motion, damage-aware rendering, wallpaper scenes, Living Tiles—and replaces the launcher-specific visual model with a simpler native grammar.

## Cookie Continuum

Cookie UI is organized around three primary visual/interaction primitives:

1. **Field** — open content space. The Field is not a card wall, page grid, or glass sheet. It carries wallpaper/context and lets semantic objects breathe without forcing every object into a container.
2. **Thread** — a stable spatial relationship that connects related objects, navigation, and transitions. A Thread can be expressed through alignment, spacing, a subtle contour, or motion path; it does not need to be a permanently visible line.
3. **Seal** — a closed trusted contour reserved for security-critical system surfaces. Seal geometry is backed by compositor ownership and capture policy, never available as app decoration.

Secondary primitives:

- **Node** — a semantic object attached to a Field/Thread: app, person, conversation, document, media, setting, action, Living Tile, or trusted system object.
- **Veil** — a transient contextual layer used sparingly for search, selection, or temporary controls. Veils must never become the dominant navigation model and must remain functional with transparency disabled.

## Home: one continuum, not pages

Cookie Home is one continuous scene, not horizontal pages plus an app drawer.

The scene has three semantic bands whose boundaries are fluid rather than panelized:

- **Now** — immediate state and timely objects. A small number of Living Tiles or semantic Nodes may appear here.
- **Pinned** — stable user-chosen Nodes. Positions remain stable enough to build spatial memory.
- **Index** — discovery entry. It is not a permanent dock. A short, reachable index marker lives near the user's preferred thumb side and opens the Cookie Index.

The user can scroll the continuum, but common actions do not require long scrolling. The Index can be summoned from the reachable side and provides fast semantic discovery.

## Cookie Index

The Cookie Index replaces the conventional app drawer.

It has three synchronized views, all backed by the same semantic search model:

- **Alphabetic** for deterministic complete browsing.
- **Intent** clusters for categories such as communicate, create, travel, money, media, device, and recent activity.
- **Search** for apps, contacts, conversations, documents, settings, actions, and local results.

The Index remembers stable category/order positions across sessions where possible. Prediction may reorder a small suggestion strip but must not continuously reshuffle the main deterministic index.

## Reachability without One UI composition

Cookie does not use oversized blank title regions or bottom-sheet-dominant navigation to solve thumb reach.

Instead:

- the Index marker can follow left/right handed preference;
- high-frequency system actions can be mirrored to the reachable side;
- Thread attachment points can shift while preserving semantic order;
- tall phones can lower interactive Nodes without moving titles into giant empty header zones;
- foldables split Fields around hinges but keep the same Thread topology.

## Distinctive visual signature

Cookie must remain recognizable in grayscale with wallpaper, transparency, blur, shadows, and depth disabled.

Recognition comes from:

- open Field composition rather than card grids;
- Thread-based alignment and connected transitions;
- sparse containment—containers only where grouping or hit-area clarity requires them;
- asymmetrical but stable Node placement;
- a consistent contour language that uses Sweep/Anchor relationships without repeated identical rounded rectangles;
- Seal geometry for trusted surfaces;
- connected handoff motion in which the selected Node remains the visual source of the destination state.

## Icons

Cookie does not mandate one universal launcher mask.

App artwork is normalized by optical area, contrast, and safe-zone rules. Cookie may provide a subtle system backing shape when required for contrast, but the backing shape is contextual rather than a permanent squircle/circle mask.

System glyphs use a restrained Cookie cut/contour construction: continuous strokes with one deliberate directional break or sweep where it improves recognition. Decorative inconsistency is rejected.

## Living Tiles

Living Tiles are Nodes, not miniature app windows and not a wall of cards.

A Living Tile may be:

- backgroundless when contrast permits;
- bounded by an Anchor/Sweep contour when interaction or grouping needs containment;
- expanded along the Thread while focused;
- collapsed back into the Field without changing its semantic identity.

Continuous arbitrary animation remains forbidden. System scheduling owns motion and refresh cadence.

## Notifications

Notifications are a **Threaded Stream**, not a vertical stack of identical cards.

Related notifications can share a common Thread anchor. Expansion reveals detail inline from the source Node. Private content disclosure remains controlled independently from geometry. Security notifications use Seal-backed trusted composition and cannot be imitated by normal apps.

## Quick Controls

Quick Controls are a **Control Weave**: compact control Nodes grouped by functional relationship, with open spacing rather than one monolithic sheet of pills.

On compact/tall phones, frequent controls bias toward the user's reachable side. On wide/foldable devices, groups recompose into two Fields connected by preserved semantic ordering.

## Motion

Cookie motion has four rules:

1. **Source continuity** — destinations emerge from the object/action that caused them.
2. **Interruptibility** — user input can retarget or reverse motion from the current visible state.
3. **Velocity continuity** — retargeting should not restart at zero velocity unless the interaction semantics require a stop.
4. **Cost hierarchy** — input response and frame pacing outrank depth, material, ambient motion, and wallpaper effects.

Motion communicates topology. It is never required to understand state.

## Forbidden convergence patterns

Cookie system scenes must reject the following as primary identity patterns:

- horizontal pages of fixed app grids;
- permanent conventional icon dock/tray;
- full-screen app drawer grid as the only discovery model;
- oversized blank title/header zones;
- repeated rounded rectangular card walls;
- bottom-sheet-dominant navigation;
- pill-heavy control surfaces;
- full-screen glass/translucency as identity;
- floating-island status/notification imitation;
- mandatory one-shape launcher icon masks;
- wallpaper-dependent recognizability;
- constantly reshuffled predictive home layouts that destroy spatial memory.

## Accessibility and reduced effects

Thread topology must survive high contrast, reduced transparency, reduced motion, screen magnification, large text, switch access, keyboard navigation, and assistive technology.

When visual Threads are hidden, semantic order and focus traversal must communicate the same relationships.

## Performance

Cookie Continuum is designed around partial redraw. Stable wallpaper and unchanged Nodes remain cacheable while one Thread/Node animates. At 90/120 Hz the renderer should prefer local/regional damage and drop optional optical effects before widening damage or increasing input age.

## Completion test

Cookie UI v1 is visually complete only when all canonical scenes—Lock, Home, Index, Notifications, Quick Controls, Settings, App Surface, Switcher—can be rendered with:

- no vendor-specific visual primitive;
- no dependency on blur/transparency;
- consistent Field/Thread/Seal grammar;
- adaptive phone/foldable geometry;
- secure-plane enforcement;
- deterministic reduced-motion/reduced-transparency behavior;
- measured frame pacing and damage bounds.
