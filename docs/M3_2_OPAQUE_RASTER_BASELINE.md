# M3.2 — Opaque-first ENML raster baseline

This note records ENML's concrete bounded CPU/economy pixel path. It is intentionally deterministic and caller-memory-owned. It is not the final GPU/material backend and does not freeze the final palette, fonts, blur kernels or shader implementation.

## Why opaque first

ENML's visual direction includes translucency, crystal-like material, depth, rich color and authored motion, but those effects are not allowed to become the only reason a control or surface is understandable. Geometry, hierarchy, focus, type and trusted state must remain clear when live backdrop effects are unavailable, disabled for accessibility, or reduced for power/frame-budget reasons.

The supplied transparency research is used as a guardrail here: figure/ground and state can become ambiguous when transparency depends too heavily on the background. ENML therefore establishes a recognizable opaque identity first and layers optical richness later.

## Current CPU/economy pipeline

```text
SemanticTree
    |
    v
RendererSnapshot
    |
    v
RenderCommandBuffer
    |
    +--> semantic ColorRole / material / depth / contour / typography
    +--> focus and state
    |
    v
shared renderer-private PixelContour evaluator
    |
    +--> material fill + tint
    +--> fixed 2x2 interior contour coverage
    +--> coverage-aware raised/floating/hero depth silhouette
    +--> directional leading/trailing edge lighting
    +--> inset-recessed edge lighting (no external shadow)
    +--> focus/outline painted last
    |
    v
complementary outside contour fringe
    |
    v
renderer-private font/paragraph/glyph path
    |
    v
caller-owned RGBA8 target
```

`RasterTheme` is renderer-owned. Applications still submit semantic color/material/typography roles rather than concrete RGB values, font files, native handles, glyph masks or shader objects.

## Current guarantees

The raster path currently provides:

- fixed caller-owned memory and no hidden raster allocation;
- maximum 4096 x 4096 target dimensions with explicit stride/capacity validation;
- rational Q6-to-pixel scaling without exposing display-device internals to applications;
- clipping to the supplied target;
- opaque semantic background and material-tint painting;
- one renderer-private physical `PixelContour` interpretation shared by fill, depth, focus boundaries and outside fringe;
- asymmetric per-corner authored contours;
- normalized fixed-point circle→squircle smoothing that remains bounded even at maximum valid logical geometry and the highest supported raster numerator;
- deterministic fixed 2x2 coverage for center-owned curved edge pixels plus complementary outside fringe coverage;
- coverage-aware opaque raised/floating/hero shadow silhouettes that darken only already-painted supporting pixels;
- **no external shadow for `DepthRole::inset`**; inset surfaces instead reverse the edge-light direction to read as recessed;
- coverage-aware leading highlight and restrained trailing occlusion for raised/floating/hero material;
- coverage-aware leading occlusion and trailing highlight for inset material;
- explicit focus/outline applied after all optical edge work so state remains dominant;
- deterministic command order inherited from `RenderCommandBuffer`;
- bounded diagnostic counters for filled surfaces, shadows, lit edges, shaded edges, partial-coverage writes and total pixel writes;
- real renderer-command text painting through bounded font/paragraph/line-metric/glyph seams;
- GCC, Clang, ASan/UBSan and native AArch64 coverage through the M3 Semantic UI matrix.

The fixed-grid contour evaluator and directional lighting are lightweight fallback primitives, not a physically based renderer. They intentionally avoid offscreen shadow surfaces, blur kernels, floating point contour math, shader compilation, worker threads or a general public path engine.

## Text state

Visible semantic text can already flow from the same deterministic `RenderCommandBuffer` through renderer-owned font resolution, paragraph shaping, real vertical metrics, bounded glyph masks and coverage-to-RGBA painting.

What ENML **does not yet claim** is a reviewed production Unicode/font backend or final platform typeface. The contracts and pixel path exist; the remaining text milestone is to integrate the selected renderer-private production provider/shaper/raster implementation without exposing font paths or native font-library handles to applications.

## Depth and lighting rule

Depth must clarify hierarchy, not become decoration that competes with interaction state.

Raised/floating/hero surfaces use three bounded cues in the economy renderer: offset support darkening, leading highlight and trailing occlusion. Inset material reverses the edge-light direction and casts no positive-offset external shadow. All edge effects are attenuated by authored contour coverage, and focus/outline is always painted afterward.

`DepthMetrics::blur_q6` remains intentionally deferred in this CPU fallback. A later bounded blur implementation or GPU backend may realize it, but the current renderer does not spend a large CPU kernel merely because a premium depth token exists.

## Trusted presentation relationship

Secure attribution is **not** another UI material token. `TrustedPresentation` and `TrustedOverlaySnapshot` are compositor-derived. The separate private `rasterize_trusted_marks()` display pass paints the baseline ENML trust signature after client composition, outside application style authority. See `docs/M3_2_TRUSTED_PRESENTATION.md`.

## What remains intentionally later

- reviewed production Unicode/font provider/shaper/raster integration;
- final font assets, hinting and color-font policy;
- adaptive/closed-form analytic contour coverage beyond the current fixed 2x2 economy baseline;
- bounded blurred shadows/depth blur;
- alpha compositing between ENML surfaces;
- live backdrop blur/filtering and context-aware translucent material response;
- gradients/image sampling and production GPU shader backend;
- final hardware-compositor integration of the trusted overlay pass;
- richer compositor-deadline-aware scene motion.

## Visual-language constraints

The opaque baseline must remain useful on low-power hardware and under reduced-transparency/high-contrast preferences. Later optical work therefore layers on these invariants rather than replacing them:

1. authored contour remains recognizable;
2. focus/state remains visible without transparency;
3. text and controls maintain figure/ground separation;
4. secure-system attribution is generated outside application pixels;
5. color supports hierarchy but is not the only state cue;
6. depth direction is internally consistent between raised and inset material;
7. motion communicates cause/effect instead of masking weak static affordance;
8. economy/balanced/full quality levels preserve one ENML identity.

A future literal screenshot should come from the real semantic→render-command→raster/compositor path. Concept art or image-generated previews are not literal renderer captures.
