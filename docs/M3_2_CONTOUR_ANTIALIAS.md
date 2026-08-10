# M3.2 — bounded contour antialias baseline

ENML's authored contours are part of the platform identity. A swept or continuous surface should not become a jagged binary silhouette merely because the device is using the CPU/economy renderer.

The CPU path now splits bounded coverage work between the primary material raster and a perimeter-only follow-up pass.

## Primary interior coverage

`rasterize_opaque_materials()` keeps its existing center-sampled ownership test, but a center-owned curved edge pixel is no longer painted as automatically 100% covered. The renderer evaluates the same contour at a fixed 2x2 subpixel grid and coverage-blends the material/focus edge into the already-painted supporting pixel when only part of that pixel belongs to the authored shape.

This matters because post-processing cannot correctly “unpaint” an interior edge after the supporting pixels have already been overwritten. Performing the partial blend at the moment the material/outline owns the pixel preserves the actual background contribution without requiring an offscreen surface.

`RasterStats::partial_coverage_writes` exposes bounded renderer-private diagnostic evidence for these primary-raster partial writes. It is not an application ABI or a visual token.

## Outside fringe

`rasterize_contour_antialias_fringe()` handles the complementary case where the pixel center is outside the authored contour but one or more fixed subpixel samples are inside.

It:

- consumes the same bounded `RenderCommandBuffer`, `RasterTheme`, and caller-owned `RasterTarget` as the opaque material stage;
- uses the same fixed 2x2 sample grid; there is no floating-point dependency, heap allocation, worker thread, shader compiler, path engine, or background cache;
- evaluates the same asymmetric per-corner radius and smoothing inputs used by the primary raster;
- writes only partial **outside** coverage where center ownership left the supporting pixel untouched;
- uses the focus or outline role for the fringe when present, otherwise the resolved material fill;
- preserves an opaque final pixel when the supporting surface is already opaque;
- rejects malformed targets, themes, commands, radii, and smoothing values through the existing raster error domain.

The outside pass is bounded to one pixel beyond the rasterized contour. Cost therefore tracks visible contour perimeter rather than turning every surface into a supersampled offscreen buffer.

`rasterize_opaque_frame()` remains the preferred CPU/economy geometry entry point. It runs the material/depth/focus stage first—now including interior edge coverage—and then the complementary outside-fringe stage in deterministic order.

## Current quality boundary

Together the two stages provide fixed-grid coverage on both sides of the visible authored silhouette. This is a meaningful step beyond the earlier outside-fringe-only baseline, but it is still not the final ENML analytic/vector renderer.

Current limits remain intentional:

- the grid is fixed 2x2 rather than adaptive analytic coverage;
- shadow silhouette coverage is still center-sampled and blur kernels do not yet exist;
- leading-edge lighting is bounded and not a physically based material model;
- material translucency and live backdrop filtering remain separate later effects;
- no public path/Bezier/vendor graphics ABI is introduced.

The next contour-quality step should share one renderer-private analytic contour evaluator between the primary material, shadow and future compositor/GPU paths instead of growing separate public geometry APIs.

## Visual-language rule

Antialiasing improves craft without changing identity. Continuous and swept contours keep their authored asymmetry and smoothing; the fallback renderer does not replace them with generic rounded rectangles.

Premium effects may degrade under capability or power pressure, but geometry, hierarchy, state and recognizable ENML character must survive that degradation.
