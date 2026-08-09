# M3.2 — bounded contour antialias baseline

ENML's authored contours are part of the platform identity. A swept or continuous surface should not become a jagged binary silhouette merely because the device is using the CPU/economy renderer.

This slice adds a deterministic antialias fringe pass after the existing opaque material raster.

## Contract

`rasterize_contour_antialias_fringe()`:

- consumes the same bounded `RenderCommandBuffer`, `RasterTheme`, and caller-owned `RasterTarget` as the opaque material stage;
- uses a fixed 2x2 subpixel sample grid; there is no floating-point dependency, heap allocation, worker thread, shader compiler, path engine, or background cache;
- evaluates the same asymmetric per-corner radius and smoothing inputs used by the opaque raster;
- writes only partial outside coverage where binary center sampling left the supporting pixel untouched;
- uses the compositor/UI-selected focus or outline role for the fringe when present, otherwise the resolved material fill;
- preserves an opaque final pixel when the supporting surface is already opaque;
- rejects malformed targets, themes, commands, radii, and smoothing values through the existing raster error domain.

The pass is intentionally bounded to one pixel outside the rasterized contour. Cost therefore tracks visible contour perimeter rather than turning every surface into a high-resolution supersampled offscreen buffer.

## What this is not

This is not the final ENML vector/path renderer. In particular:

- interior edge pixels are still owned by the existing center-sampled opaque raster;
- shadow kernels and lighting are not supersampled by this pass;
- material translucency and live backdrop filtering remain separate later effects;
- this does not introduce a public path/Bezier/vendor graphics ABI.

The purpose is to improve the economy/CPU baseline now while preserving a clean migration path to higher-quality analytic/vector coverage later.

## Visual-language rule

Antialiasing must improve craft without changing identity. Continuous and swept contours keep their authored asymmetry and smoothing; the fallback renderer does not replace them with generic rounded rectangles.

This remains consistent with the project vision: premium effects are allowed to degrade under capability or power pressure, but geometry, hierarchy, state and recognizable ENML character must survive that degradation.
