# M3.2 — Bounded Semantic UI and Accessibility Foundation

M3.2 begins the application-facing semantic UI layer above the M3 compositor/pixel-buffer substrate. The central rule is that meaning, state, focus, accessibility and responsive layout must exist independently of rendered pixels.

## Initial architecture

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
        v
 future design-system renderer
        |
        v
 M3 compositor surfaces + BufferId
```

The compositor remains a mechanism/ownership layer. It does not infer buttons, text fields, selected state, accessibility labels or application navigation from pixels.

## Bounded semantic tree

The initial `core/osui` library introduces:

- strong `UiNodeId`
- 256-node fixed global tree capacity per tree instance
- at most 32 direct children per node
- maximum semantic depth 16
- fixed 160-byte UTF-8 semantic text objects
- monotonic node IDs within a tree instance; deleted IDs are not recycled
- roles: root, container, text, image, button, toggle, text field, list and list item
- explicit visible/enabled/focused/selected/checked/pressed state
- explicit typed actions: activate, focus, toggle, set text and select
- independent `StyleTokenId` references instead of hard-coded colors/fonts/shapes in semantic structure
- bounded subtree removal and stale-ID rejection

Interactive nodes that participate in accessibility require semantic labels. Nodes deliberately hidden from accessibility cannot advertise public actions.

## Accessibility projection

`SemanticTree::accessibility_snapshot()` produces a fixed-capacity semantic snapshot without examining framebuffer contents.

The projection:

- includes only effectively visible nodes;
- omits nodes marked `accessibility_hidden`;
- re-parents accessible descendants to the nearest accessible ancestor when a purely decorative grouping node is omitted;
- carries role, logical bounds, state, actions and semantic label;
- uses deterministic node-ID order.

This is only the foundation for the later platform accessibility service/bridge. It deliberately establishes that accessibility information originates at the semantic UI boundary, not from OCR or pixel inspection.

## Logical geometry

M3.2 uses deterministic fixed-point logical geometry: 64 logical units equal one density-independent logical pixel. Physical-pixel conversion remains platform-owned.

The initial responsive list/detail helper accepts a trusted logical viewport with safe insets plus an explicit policy. It can deterministically choose a single-pane or dual-pane composition without asking applications to identify a physical device class or Linux display node.

The default 600dp dual-pane breakpoint is provisional policy evidence inspired by the supplied multi-screen UI reference, not a permanent Android-compatible ABI. The breakpoint and pane constraints are explicit data and can evolve at the design-system layer.

## Focus and actions

Focus is tree-owned and unique. A node can receive focus only when:

- its role/action contract permits focus;
- it is enabled;
- it and all ancestors are visible.

`dispatch_action()` validates that a requested semantic action belongs to the target role and that the node is currently enabled/effectively visible before emitting a typed `UiEvent`. This is not yet the hardware input router; it is the semantic contract that the later router will target after trusted hit testing.

## Reference-driven decisions

The supplied *Android UI Design Basics* material is useful here for enduring principles: standard reusable controls improve consistency; UI is hierarchical but unnecessarily complex hierarchies hurt rendering performance; logical/density-independent sizing and responsive recomposition are necessary across screen sizes; UI events belong to semantic objects; structure should be separated from themes/styles; list rows should be reused/virtualized; and blocking remote/media work should stay off the interactive UI path.

ENML deliberately does not copy Android Activity/Fragment lifecycle, XML resource ABI, Java listeners, Intent routing, resource qualifiers, Holo themes or NDK graphics exposure. See `docs/REFERENCE_ANDROID_UI_DESIGN.md`.

BlackBerry/One UI and the 2025/2026 UI-perception references continue to guide predictable components, reachability, accessibility, explicit state communication and restraint around visually ambiguous transparency. Those remain design-system semantics rather than compositor ABI.

## Current limits / next work

This first M3.2 slice does not yet include:

- actual widget rendering;
- text shaping/font stack;
- scroll physics;
- virtualized collection data source protocol;
- hardware input service/router;
- accessibility service IPC;
- semantic tree serialization/OSIDL;
- shell widgets;
- theme color/typography token definitions;
- animation/motion engine.

The next M3.2 work should add bounded collection virtualization and a renderer-facing immutable semantic snapshot before wiring public app UI API/OSIDL.
