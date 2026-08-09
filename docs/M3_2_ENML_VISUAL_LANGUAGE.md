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

Curves are a first-class part of the ENML language. `CurveRole` is separate from the legacy simple radius role so later renderers can support continuous and swept contours, asymmetry, custom paths and characteristic transitions between edges.

A future ENML renderer should be able to make a panel identifiable from its contour and motion even when color is removed.

### Depth with purpose

Depth is not decoration. Flush, inset, raised, floating and hero roles should tell the user which plane owns an interaction and which content is transient or dominant. Shadows, occlusion, scale and lighting are renderer techniques, not semantic state.

Secure system UI must remain visually attributable to trusted system principals. A richer material system must not make application surfaces capable of visually impersonating secure system surfaces.

### Color as composition

ENML is not constrained to monochrome minimalism. The design system includes multiple semantic accent roles so themes can compose richer palettes and gradients without baking concrete colors into application semantics.

Color still cannot be the only carrier of state. Focus, selection, error and security attribution require redundant geometry, text, iconography or motion cues.

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
- semantic accessibility independent of rendered pixels.

A future low-power or low-capability renderer should use the same principle: lower optical complexity while preserving semantic hierarchy, geometry, state, focus and timing order.

## Current code boundary

The current M3.2 design layer adds semantic roles for:

- color;
- typography;
- spacing;
- simple shape radius;
- optical material;
- depth;
- authored curve families;
- motion;
- material tint;
- user visual preferences.

`StyleTokenId` remains the semantic tree's only style reference. Applications do not receive shader parameters, compositor internals, physical pixel density, Linux display handles, GPU resources or vendor theme implementation details.

The intended path is:

```text
semantic node + StyleTokenId
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
preference + capability resolution
          |
          v
renderer-owned concrete palette / paths / shaders / animation
          |
          v
M3 compositor surface + BufferId
```

## Reference guidance incorporated

The supplied BlackBerry UI material is useful for status visibility, direct feedback, recoverability, progressive disclosure, responsive task organization and the general idea that premium visual quality can use material, lighting, texture and depth. ENML does not copy BlackBerry component visuals or historical theme assets.

The supplied Figma material is useful as a construction workflow for opacity, gradients, corner shaping, blur, shadows, component organization and prototyping. Figma is a design tool in the workflow, not the source of ENML's identity.

The supplied UX material reinforces desirability, emotional response, usability testing, visual representation, timing and motion as parts of interaction design. Those principles justify investing in craft while retaining clarity and user control.

The supplied recent mobile UI/UX review supports micro-interaction feedback, inclusive design, responsive/mobile-first composition and the need to balance richer visual techniques against performance and battery cost.

The supplied natural-interface guidance is useful for discoverability, immediate visual feedback, state communication, motion that maps to user action and reduced fatigue. ENML can apply those lessons to touch, pointer, keyboard, voice and later sensor-driven interaction without inheriting Kinect-specific UI.

## Next renderer work

This document does not claim that the final visual renderer exists yet. The next visual implementation slices should remain small and testable:

1. immutable renderer command/snapshot representation for resolved style roles;
2. deterministic curve/path primitives independent of vendor graphics APIs;
3. text shaping/font fallback and large-text reflow;
4. bounded 2D material renderer with opaque fallback first;
5. composited translucency and blur with explicit capability/power budgets;
6. motion scheduler tied to frame deadlines and reduced-motion policy;
7. design-system components built from semantic roles rather than raw paint commands;
8. Figma reference file using the same token vocabulary so implementation and design stay aligned;
9. secure-system visual attribution that applications cannot request or counterfeit;
10. usability and accessibility evaluation before the visual language is frozen.

The visual identity should remain flexible while these mechanisms mature. ENML should become distinctive through a coherent combination of contour, material, color, typography, depth and motion rather than through imitation of an existing operating system.
