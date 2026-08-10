# M3.2 — bounded contour antialias baseline

ENML's authored contours are part of the platform identity. A swept or continuous surface should not become a jagged binary silhouette merely because the device is using the CPU/economy renderer.

The CPU path now uses one renderer-private physical contour evaluator for material ownership, interior edge coverage, opaque depth silhouettes and the complementary outside fringe.

## Shared physical contour evaluator

`os/ui/detail/contour_geometry.hpp` is renderer-private implementation detail rather than an application-facing geometry ABI. It lowers a validated semantic `RenderCommand` plus `RasterScale` into one `PixelContour` and provides the shared center/2x2 coverage/boundary predicates used by both `raster.cpp` and `contour_aa.cpp`.

That shared evaluator preserves:

- per-corner asymmetric radii;
- authored smoothing from circular toward squircle-like coverage;
- deterministic Q6→physical-pixel scaling;
- the same one-pixel fixed-grid coverage rules across material, focus, depth and outside-fringe stages.

The smoothing test is evaluated in normalized Q10 coordinate space before fourth-power terms are formed. The older direct fourth-power expression could wrap `uint64_t` for a very large but still valid logical contour at the highest supported raster scale. Normalization keeps intermediate values bounded without floating point, compiler-specific 128-bit arithmetic or a general path engine. A unit test exercises the maximum valid logical contour at the maximum supported raster numerator.

## Primary interior coverage

`rasterize_opaque_materials()` keeps center ownership as the rule for the primary surface, but a center-owned curved edge pixel is not automatically painted as 100% covered. The renderer evaluates the shared contour at a fixed 2x2 subpixel grid and coverage-blends the material/focus edge into the already-painted supporting pixel when only part of that pixel belongs to the authored shape.

This matters because post-processing cannot correctly “unpaint” an interior edge after the supporting pixels have already been overwritten. Performing the partial blend at the moment the material/outline owns the pixel preserves the actual background contribution without requiring an offscreen surface.

`RasterStats::partial_coverage_writes` exposes bounded renderer-private diagnostic evidence for these primary-raster and shadow partial writes. It is not an application ABI or a visual token.

## Coverage-aware opaque depth

The opaque/economy depth fallback now uses the same shared contour evaluator after applying the deterministic positive depth offset. Partial swept/continuous shadow pixels attenuate the requested darkening strength by their 2x2 coverage rather than turning the shadow silhouette into a binary staircase.

This remains intentionally lightweight: it darkens only already-painted supporting pixels, creates no opaque backdrop where none exists, allocates no offscreen shadow surface, and introduces no blur kernel or graphics worker. Focus/outline is still painted after depth and lighting so state remains the final unambiguous cue.

## Outside fringe

`rasterize_contour_antialias_fringe()` handles the complementary case where the pixel center is outside the authored contour but one or more fixed subpixel samples are inside.

It:

- consumes the same bounded `RenderCommandBuffer`, `RasterTheme`, and caller-owned `RasterTarget` as the opaque material stage;
- uses the same shared fixed 2x2 evaluator; there is no floating-point dependency, heap allocation, worker thread, shader compiler, path engine, or background cache;
- evaluates the same asymmetric per-corner radius and smoothing inputs used by the primary material/depth raster;
- writes only partial **outside** coverage where center ownership left the supporting pixel untouched;
- uses the focus or outline role for the fringe when present, otherwise the resolved material fill;
- preserves an opaque final pixel when the supporting surface is already opaque;
- rejects malformed targets, themes, commands, radii, and smoothing values through the existing raster error domain.

The outside pass is bounded to one pixel beyond the rasterized contour. Cost therefore tracks visible contour perimeter rather than turning every surface into a supersampled offscreen buffer.

`rasterize_opaque_frame()` remains the preferred CPU/economy geometry entry point. It runs material/depth/focus first and then the complementary outside-fringe stage in deterministic order.

## Current quality boundary

Together the stages provide one geometric truth and fixed-grid coverage on both sides of the visible authored silhouette. This is still not the final ENML analytic/vector renderer.

Current limits remain intentional:

- coverage is fixed 2x2 rather than adaptive/closed-form analytic coverage;
- shadow blur kernels do not yet exist;
- leading-edge lighting is bounded and not a physically based material model;
- material translucency and live backdrop filtering remain separate later effects;
- no public path/Bezier/vendor graphics ABI is introduced.

The next contour/depth quality step should build on this shared renderer-private evaluator rather than introduce another geometry interpretation. A future compositor/GPU backend can either consume equivalent validated contour parameters or replace the coverage backend while preserving the same semantic contour contract.

## Visual-language rule

Antialiasing improves craft without changing identity. Continuous and swept contours keep their authored asymmetry and smoothing; the fallback renderer does not replace them with generic rounded rectangles.

Premium effects may degrade under capability or power pressure, but geometry, hierarchy, state and recognizable ENML character must survive that degradation.
