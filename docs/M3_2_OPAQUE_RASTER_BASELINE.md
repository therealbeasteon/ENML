# M3.2 — Opaque-first ENML raster baseline

This slice is the first concrete pixel-writing stage in M3.2. It is intentionally small, deterministic and CPU-side. It is not the final ENML renderer and it does not define the final palette, typography, blur, lighting or shader implementation.

## Why opaque first

ENML's visual direction includes transparency, translucency, crystal-like material, depth, rich color and authored motion. Those effects are not allowed to become the only reason a control or surface is understandable.

The supplied transparency research shows that layered transparency can make figure/ground separation and button state harder to perceive depending on background context, color quantity, reference points and conventions. The first concrete renderer therefore proves geometry, contour, hierarchy, focus and palette-role mapping with opaque output before live backdrop sampling is introduced.

This also follows the supplied interaction guidance that feedback and state should be immediately understandable, and the security guidance that important system state should be visible and intelligible without demanding expert knowledge.

## Pipeline

```text
SemanticTree
    |
    v
RendererSnapshot
    |
    v
RenderCommandBuffer
    |
    +--> semantic ColorRole
    +--> material/depth/curve/motion intent
    +--> resolved per-corner contour
    +--> focus/state
    |
    v
RasterTheme (renderer-owned concrete colors)
    |
    v
rasterize_opaque_materials()
    |
    v
caller-owned RGBA8 pixel target
```

`RasterTheme` is renderer-owned. Applications still never submit RGB values as UI ABI. A future platform theme can change concrete color values without changing `StyleTokenId`, `ColorRole`, application semantics or the public app contract.

## Current guarantees

The rasterizer currently provides:

- fixed caller-owned memory; no hidden allocation;
- maximum 4096 x 4096 bounded target dimensions;
- explicit stride and target-capacity validation;
- rational Q6-to-pixel scale for 1x/2x/3x-style density mapping without exposing device nodes to applications;
- clipping to the supplied raster target;
- opaque semantic background fill;
- renderer-owned material tint blending;
- per-corner radius coverage preserving asymmetric swept contours;
- explicit one-pixel outline/focus treatment;
- deterministic command order inherited from `RenderCommandBuffer`;
- validation errors for malformed target, theme or command data;
- 64-bit pixel-write accounting;
- GCC, Clang, ASan/UBSan and native AArch64 CI coverage through `ui_raster_test`.

The rasterizer deliberately ignores live opacity/backdrop blur at this stage. A `crystal` semantic material is therefore painted as an opaque fallback using its background/tint roles and authored contour. This is a feature of this milestone, not a claim that crystal material is ultimately opaque.

## What is intentionally not implemented yet

- font glyph rasterization;
- production font assets or shaping backend;
- paragraph bidi/line breaking;
- continuous-curve smoothing beyond the current per-corner coverage baseline;
- shadows and depth blur;
- specular/highlight lighting model;
- alpha compositing between ENML surfaces;
- live backdrop blur/filtering;
- gradients or image sampling;
- GPU path/shader backend;
- compositor-deadline-aware animation.

## Visual-language constraints

The opaque baseline must remain useful when advanced optical features are unavailable. That protects the project vision on low-power hardware and under reduced-transparency/high-contrast accessibility preferences.

Later material work should therefore layer optical richness on top of these invariants rather than replacing them:

1. authored contour remains recognizable;
2. focus/state remains visible without transparency;
3. text and controls maintain figure/ground separation;
4. secure-system attribution remains independently recognizable;
5. color supports hierarchy but is not the only state cue;
6. motion communicates cause/effect instead of masking weak static affordance;
7. economy/balanced/full quality levels preserve one ENML identity.

## Next raster work

The next renderer slices should add, in order:

1. improved continuous/swept contour coverage and anti-aliasing;
2. renderer-owned font provider and production shaping integration;
3. glyph-mask/text rasterization and large-text reflow integration;
4. depth edge/shadow primitives with bounded kernels;
5. secure-system attribution primitives that application style tokens cannot request;
6. alpha compositing and context-aware translucent material;
7. bounded backdrop filtering and lighting/specular response;
8. compositor-deadline-aware motion.

A screenshot produced before these steps is a preview of direction. Once a scene is emitted through this raster path and written into the compositor buffer, it becomes an actual ENML render rather than a mockup.
