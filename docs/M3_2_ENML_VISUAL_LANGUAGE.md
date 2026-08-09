# M3.2 — ENML Visual Language Direction

ENML's visual system must be recognizably its own. Reference material is design evidence, not a template, compatibility target, or permission to reproduce another platform's visual identity.

The target character is classic, crafted and luxurious without becoming ornamental for its own sake. The interface may use strong color, translucency, depth, expressive curves, dimensional lighting and animation, but those effects must communicate hierarchy, state or motion and must remain compatible with accessibility, low idle work and bounded rendering cost.

## Non-derivative rule

ENML may reuse familiar interaction semantics where familiarity reduces cognitive load: a button behaves like an action, a toggle has an explicit state, focus is visible, back/forward navigation is understandable, errors are recoverable and long work provides feedback.

ENML must not reproduce the visual grammar of Android/Material, iOS, BlackBerry, Windows, One UI, or any other vendor platform. In particular:

- no vendor component silhouettes as the default ENML component language;
- no copied icon sets, proprietary visual metaphors, transition signatures or branded color systems;
- no theme whose identity depends on imitating a historical OS;
- no "glass" effect copied wholesale from an existing platform;
- no assumption that generic rounded rectangles are sufficient as a signature shape language.

References should answer *why* an interaction or visual technique works. ENML owns the *what it looks and feels like* decision.

## Visual character

### Crafted materiality

ENML should feel constructed rather than painted flat. Material roles can express opaque, translucent, crystal-like, smoked and luminous surfaces. The renderer may use tint, optical blur, specular response, edge treatment, depth and contrast to distinguish them.

The current code deliberately expresses these as semantic optical roles rather than fixed RGB or shader ABI. This keeps the visual identity evolvable while allowing the renderer to establish a coherent material hierarchy.

### Curves with authorship

Curves are a first-class part of the ENML language. `CurveRole` is separate from the legacy simple radius role so renderers can support continuous and swept contours, asymmetry, custom paths and characteristic transitions between edges.

The current renderer boundary resolves bounded contour intent without binding public UI semantics to a vendor path or GPU API. The asymmetric swept family is deliberately present so the language is not reduced to generic rounded rectangles.

A future ENML renderer should be able to make a panel identifiable from its contour and motion even when color is removed.

### Depth with purpose

Depth is not decoration. Flush, inset, raised, floating and hero roles should tell the user which plane owns an interaction and which content is transient or dominant. Shadows, occlusion, scale and lighting are renderer techniques, not semantic state.

Secure system UI must remain visually attributable to trusted system principals. A richer material system must not make application surfaces capable of visually impersonating secure system surfaces.

### Color as composition

ENML is not constrained to monochrome minimalism. The design system includes multiple semantic accent roles so themes can compose richer palettes and gradients without baking concrete colors into application semantics.

Color still cannot be the only carrier of state. Focus, selection, error and security attribution require redundant geometry, text, iconography or motion cues.

### Typography as craft and infrastructure

Typography is part of ENML's identity, but applications must not choose platform font files or vendor family names as public ABI. The renderer now carries bounded semantic font-family roles and fallback order rather than concrete file paths or font handles.

`interface` and `display` are roles, not a requirement to use unrelated families. A final ENML theme can map them to coordinated cuts of one type family, preserving the cohesive readability recommended by the reference material while still allowing titles and hero surfaces to have a more authored character. International and symbol fallbacks are platform-owned so localization does not depend on every application bundling its own font strategy.

Text scaling remains independent of the concrete font asset. The current design metrics support 100% through 300%, and later shaping/measurement must reflow rather than clip or silently shrink important text.

### Motion as interaction physics

Motion is a semantic design-system role rather than an arbitrary per-widget duration. Micro, responsive, transition and reveal roles establish different timing intentions. Renderer implementations can then tune curves, spring behavior, spatial continuity and frame pacing consistently.

Motion should show cause and effect: where a surface came from, what changed, which object owns the result and where focus moved. Decorative motion that obscures state or delays interaction is contrary to ENML's goals.

Reduced-motion mode keeps feedback but removes spatial motion and shortens the timing contract.

## Accessibility and fallback are part of the same design

Translucency, blur and motion are optional expressions of the design language, not prerequisites for understanding it.

The design resolver currently supports:

- reduced-transparency fallback to opaque material metrics;
- reduced-motion fallback to short non-spatial feedback;
- high-contrast resolution that suppresses material tint and live translucency;
- text scaling from 100% through 300%;
- minimum logical touch targets;
- semantic accessibility independent of rendered pixels;
- explicit economy/balanced/full optical quality tiers that preserve contour, hierarchy and state while reducing expensive backdrop/specular work.

A low-power or low-capability renderer should lower optical complexity while preserving semantic hierarchy, geometry, state, focus and timing order. It must not fall back to a visually unrelated second design system.

## Current code boundary

The current M3.2 design and renderer-intent layers provide semantic roles for:

- color;
- typography;
- spacing;
- simple shape radius;
- optical material;
- depth;
- authored curve families;
- motion;
- material tint;
- user visual preferences;
- platform-owned font family/fallback roles;
- renderer quality budgeting.

`StyleTokenId` remains the semantic tree's only style reference. Applications do not receive shader parameters, compositor internals, physical pixel density, Linux display handles, GPU resources, font paths or vendor theme implementation details.

The implemented path is now:

```text
semantic node + StyleTokenId
          |
          v
immutable RendererSnapshot
          |
          v
platform style token
          |
          +--> color role
          +--> typography / spacing
          +--> optical material role
          +--> depth role
          +--> curve role
          +--> motion role
          |
          v
preference + quality resolution
          |
          +--> bounded contour intent
          +--> scaled typography metrics
          +--> platform font fallback roles
          |
          v
bounded deterministic RenderCommandBuffer
          |
          v
future renderer-owned palette / font assets / shaping / paths / shaders
          |
          v
M3 compositor surface + BufferId
```

The renderer command buffer validates the semantic snapshot, resolves effective visibility, preserves deterministic node order and carries no concrete vendor graphics or font handles. Semantic accessibility labels are also kept distinct from visible control text; only actual text-role content is currently promoted to renderer text intent.

## Reference guidance incorporated

The supplied BlackBerry UI material is useful for status visibility, direct feedback, recoverability, progressive disclosure, responsive task organization, readable scalable typography and the general idea that premium visual quality can use material, lighting, texture and depth. ENML does not copy BlackBerry component visuals, fonts, icons or historical theme assets.

The supplied Figma material is useful as a construction workflow for opacity, gradients, corner shaping, blur, shadows, component organization and prototyping. Figma is a design tool in the workflow, not the source of ENML's identity.

The supplied UX material reinforces desirability, emotional response, usability testing, visual representation, timing and motion as parts of interaction design. Those principles justify investing in craft while retaining clarity and user control.

The supplied recent mobile UI/UX review supports micro-interaction feedback, inclusive design, responsive/mobile-first composition and the need to balance richer visual techniques against performance and battery cost.

The supplied natural-interface guidance is useful for discoverability, immediate visual feedback, state communication, motion that maps to user action and reduced fatigue. ENML can apply those lessons to touch, pointer, keyboard, voice and later sensor-driven interaction without inheriting Kinect-specific UI.

## Next renderer work

This document does not claim that the final pixel renderer exists yet. Completed M3.2 mechanisms now include the immutable renderer snapshot/delta, deterministic resolved command buffer, authored contour intent, visual quality fallback and semantic font fallback boundary.

The next visual implementation slices should remain small and testable:

1. bounded text shaping/measurement output above platform-owned font assets, including localization and large-text reflow;
2. bounded 2D material renderer with opaque fallback first;
3. collection data-source/recycling identity contract above the existing window/recycler slots;
4. composited translucency and blur with explicit capability/power budgets;
5. motion scheduler tied to compositor frame deadlines and reduced-motion policy;
6. design-system components built from semantic roles rather than raw paint commands;
7. Figma reference file using the same token vocabulary so implementation and design stay aligned;
8. secure-system visual attribution that applications cannot request or counterfeit;
9. usability and accessibility evaluation before the visual language is frozen.

The visual identity should remain flexible while these mechanisms mature. ENML should become distinctive through a coherent combination of contour, material, color, typography, depth and motion rather than through imitation of an existing operating system.
