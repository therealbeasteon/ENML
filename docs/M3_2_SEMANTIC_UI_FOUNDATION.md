# M3.2 — Bounded Semantic UI and Accessibility Foundation

M3.2 begins the application-facing semantic UI layer above the M3 compositor/pixel-buffer substrate. The central rule is that meaning, state, focus, accessibility and responsive layout must exist independently of rendered pixels.

## Architecture

```text
application state/controller
        |
        v
   SemanticTree (bounded)
        |
        +--> accessibility projection
        |
        +--> focus / typed actions
        |
        +--> logical responsive layout
        |
        +--> collection virtualization window
        |
        +--> immutable RendererSnapshot + bounded RendererDelta
        |
        v
 StyleTokenId / design-system resolver
        |
        +--> typography / spacing / color roles
        +--> optical material / depth / curve / motion roles
        +--> visual accessibility preferences
        |
        v
 deterministic RenderCommandBuffer
        |
        +--> resolved contour intent
        +--> resolved typography metrics
        +--> resolved optical/depth/motion intent
        +--> explicit quality-budget fallback
        |
        v
 future concrete 2D/material/text renderer
        |
        v
 M3 compositor surfaces + BufferId
```

The compositor remains a mechanism/ownership layer. It does not infer buttons, text fields, selected state, accessibility labels, application navigation or design-system meaning from pixels.

## Bounded semantic tree

`core/osui` currently provides:

- strong `UiNodeId` and separate `StyleTokenId`;
- 256-node fixed tree capacity per tree instance;
- at most 32 direct children per node;
- maximum semantic depth 16;
- fixed 160-byte validated UTF-8 semantic text;
- monotonic node IDs within a tree instance; deleted IDs are not recycled;
- roles: root, container, text, image, button, toggle, text field, list and list item;
- explicit visible/enabled/focused/selected/checked/pressed state;
- explicit typed actions: activate, focus, toggle, set text and select;
- bounded subtree removal and stale-ID rejection;
- renderer revision tracking and bounded dirty/removal metadata.

Interactive nodes that participate in accessibility require semantic labels. Nodes deliberately hidden from accessibility cannot advertise public actions.

## Accessibility projection

`SemanticTree::accessibility_snapshot()` produces a fixed-capacity semantic snapshot without examining framebuffer contents.

The projection:

- includes only effectively visible nodes;
- omits nodes marked `accessibility_hidden`;
- re-parents accessible descendants to the nearest accessible ancestor when a decorative grouping node is omitted;
- carries role, logical bounds, state, actions and semantic label;
- uses deterministic node-ID order.

This is the foundation for a later platform accessibility service/bridge. Accessibility information originates at the semantic UI boundary, not from OCR or pixel inspection.

## Renderer snapshot and invalidation

`SemanticTree::renderer_snapshot()` returns an immutable, fixed-capacity semantic snapshot for a renderer-facing consumer. `take_renderer_delta()` provides bounded changed/removed node metadata plus a revision number.

Dirty metadata is only an optimization hint. If removal bookkeeping overflows before a consumer drains it, `full_resync_required` is asserted and the complete renderer snapshot remains authoritative. A renderer must never infer missing semantic state from stale pixels.

## Deterministic renderer command lowering

`build_render_commands()` now lowers a validated `RendererSnapshot` into a fixed-capacity `RenderCommandBuffer`. This is still renderer intent rather than a paint/GPU ABI.

The lowering contract:

- validates the bounded snapshot, unique non-zero node IDs, one root, parent/depth relationships, valid UTF-8, bounds and style references;
- rejects fabricated parent cycles or malformed renderer snapshots rather than trusting arbitrary descriptors;
- computes effective visibility through the ancestor chain so hidden containers suppress descendant paint intent;
- sorts commands deterministically by monotonic `UiNodeId` rather than depending on backing-slot order;
- resolves `StyleTokenId` through visual preferences, contour geometry and scaled typography metrics;
- emits at most one renderer command per styled semantic node, preserving the 256-node bound;
- treats style ID zero as semantic-only/unstyled, so accessibility/interaction nodes can exist without requiring paint;
- carries semantic role/state and focus visibility but no RGB values, glyph IDs, shader handles, textures, physical pixels or vendor graphics objects.

Semantic labels are deliberately not assumed to be visible control text. Until the app-facing content contract distinguishes visual content from accessibility naming, only `UiRole::text` is copied into `RenderCommand::visual_text`. This prevents the renderer foundation from freezing an accessibility label into the visual-content ABI by accident.

`VisualQualityTier` is also explicit. Full quality preserves the resolved optical intent. Balanced quality bounds expensive blur. Economy quality disables live backdrop work and reduces secondary optical blur/specular cost while preserving the same material family, contour, hierarchy and semantic state. Quality fallback therefore changes implementation cost, not ENML's identity.

## Logical geometry and reflow

M3.2 uses deterministic fixed-point logical geometry: 64 logical units equal one density-independent logical pixel. Physical-pixel conversion remains platform-owned.

The responsive list/detail helper accepts a trusted logical viewport with safe insets plus an explicit policy. It deterministically chooses single-pane or dual-pane composition without asking applications to identify a physical device class or Linux display node.

The default 600dp dual-pane breakpoint remains provisional policy evidence, not an Android-compatible ABI. The breakpoint and pane constraints are explicit data and can evolve at the design-system layer.

Text design metrics currently support 100% through 300% scaling. Tests verify that large text increases row extent and collection reflow while stable semantic node identity is preserved during phone/tablet-like recomposition.

## Bounded collection virtualization

`plan_collection_window()` provides a fixed-capacity virtualization contract for large logical collections without materializing unbounded semantic children.

The current contract:

- permits up to 1,000,000 logical items;
- keeps materialized items bounded by the semantic child limit;
- supports explicit bounded overscan;
- uses 64-bit logical content/scroll extent for deep lists;
- keeps per-item materialized coordinates near the viewport rather than requiring enormous node geometry;
- rejects overscroll, impossible viewports and windows that would exceed the materialized budget.

This is a layout/planning primitive. A later data-source/object protocol still needs to define item identity, recycling and mutation across application/service boundaries.

## Focus and actions

Focus is tree-owned and unique. A node can receive focus only when:

- its role/action contract permits focus;
- it is enabled;
- it and all ancestors are visible.

`dispatch_action()` validates that a requested semantic action belongs to the target role and that the node is currently enabled/effectively visible before emitting a typed `UiEvent`. This is not yet the hardware input router; it is the semantic contract that the later router will target after trusted hit testing.

## ENML design-system boundary

The design-system layer intentionally separates semantic structure from visual identity. `StyleTokenId` resolves to platform-owned roles rather than fixed vendor colors or graphics ABI.

The current token vocabulary includes:

- semantic color roles with multiple accent roles;
- typography and spacing roles;
- legacy simple radius roles;
- optical material roles: none, opaque, translucent, crystal, smoked and luminous;
- depth roles: flush, inset, raised, floating and hero;
- authored curve roles: rectilinear, soft, continuous, swept and capsule;
- motion roles: none, micro, responsive, transition and reveal;
- material tint roles;
- reduced-transparency, reduced-motion and high-contrast resolution;
- quality-budget lowering that preserves authored geometry and hierarchy.

This is deliberately not a copy of Material Design, iOS, BlackBerry, Windows, One UI or another vendor system. See `docs/M3_2_ENML_VISUAL_LANGUAGE.md`.

The concrete palette, path tessellation, text shaping, shaders and animation engine remain renderer-owned implementation details rather than application ABI.

## Reference-driven decisions

The supplied *Android UI Design* material is useful for enduring principles: reusable semantic controls, hierarchical but performance-conscious UI trees, logical/density-independent sizing, responsive recomposition, semantic events, separation of structure from theme, and list recycling. ENML does not copy Android Activities/Fragments/XML resources/listeners/Intent routing or visual identity.

The supplied BlackBerry UI guidance is useful for task-focused organization, visible state, feedback, recoverability, progressive disclosure, readable scalable fonts, accessibility and the general principle that material, lighting, texture and depth can contribute to perceived craftsmanship. ENML does not copy BlackBerry components, icons or historical themes.

The supplied Figma reference informs the design workflow and the construction vocabulary for opacity, gradients, curves, blur, shadows, components, prototyping and design/developer collaboration. Figma is a workflow tool, not the source of ENML visual identity.

The supplied UX and natural-interface references reinforce desirability, aesthetics, timing, motion, discoverability, direct feedback and user testing. The recent mobile UI/UX review also reinforces micro-interactions, inclusive design, responsive composition and the requirement to balance richer visuals against performance and battery cost.

Those references guide why the renderer carries semantic hierarchy, immediate feedback intent, scalable typography and graceful quality fallbacks. They do not define ENML's concrete silhouettes, palette, animation signature or optical implementation.

## Current limits / next work

M3.2 does not yet include:

- actual widget/pixel rendering;
- glyph shaping or font fallback resolution;
- scroll physics;
- collection data-source/recycling protocol;
- hardware input service/router;
- accessibility service IPC;
- semantic tree serialization/OSIDL;
- shell widgets;
- concrete ENML palette/font assets;
- path tessellation or GPU shader implementation;
- animation scheduler tied to compositor frame deadlines.

The next M3.2 work should establish a bounded platform-owned font/fallback contract and text-layout/shaping boundary without baking font files or vendor typography into app ABI, then continue into collection recycling and concrete opaque-first material rendering. Hardware-specific graphics work remains behind the private renderer/display layer.
