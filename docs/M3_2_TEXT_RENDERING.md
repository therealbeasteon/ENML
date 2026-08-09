# M3.2 — bounded text rendering path

This note records the current renderer-private text path and the limits that keep it aligned with ENML's product vision.

## Pipeline

The current CPU/economy production-oriented path is:

`SemanticText + TypographyRole`
→ `ResolvedTextStyle`
→ semantic `FontFallbackChain`
→ renderer-owned `FontProviderBackend`
→ opaque `FontFaceSet`
→ renderer-owned paragraph/shaping backend
→ validated `ShapedText`
→ renderer-owned font line metrics
→ renderer-owned `GlyphMaskProviderBackend`
→ bounded glyph coverage
→ `RenderCommandBuffer` text placement
→ ENML raster target

Applications do not submit font paths, vendor family names, native font-library handles, glyph IDs, glyph bitmaps, baseline positions or font metric tables. Those remain renderer implementation details.

## Unicode and paragraph layout

ENML does not implement a partial home-grown bidi or script-shaping algorithm in the core UI layer. `FontAwareParagraphShaperBackend` is the integration boundary for a reviewed production text engine.

The platform passes explicit width and line limits plus wrap/overflow/base-direction intent. Backend output is accepted only when it satisfies the existing UTF-8, run, glyph, fallback-family and shaped-line invariants and remains within the requested paragraph bounds.

Each shaped line is normalized into renderer paint order by the backend adapter. `TextDirection` remains semantic metadata rather than an excuse for application-controlled positioning.

## Renderer-command text integration

`rasterize_render_command_text()` now consumes visible `RenderContentKind::text` commands directly. It does not create a second application-owned text display list.

For each bounded text command the renderer:

1. uses the command bounds as the paragraph width/height budget;
2. resolves only the semantic fallback families already attached to the command;
3. shapes through the renderer-private paragraph backend;
4. asks the renderer-private font backend for vertical line metrics at the semantic typography size;
5. derives the first baseline from actual ascent/descent plus bounded leading rather than a guessed fixed percentage;
6. paints validated glyph masks in the command's semantic foreground color.

When several fallback families appear in one paragraph, baseline placement uses the maximum validated ascent/descent required by the shaped glyph families. A malformed font whose ink metrics cannot fit the semantic line box is rejected rather than silently overlapping adjacent lines.

`rasterize_opaque_frame_with_text()` composes the existing opaque material/contour path and this text path in one deterministic frame entry point. Geometry is painted first and text is painted afterward. The old geometry-only entry point remains available for tests and renderer stages that intentionally have no font backend.

## Glyph raster boundary

`GlyphMaskProviderBackend` receives only a validated opaque `FontFaceDescriptor`, semantic typography metrics, one renderer-private glyph ID and the target raster scale. It returns a transient coverage view consumed synchronously.

Current limits:

- glyph masks are at most 512 × 512 pixels;
- mask stride and byte capacity are validated before use;
- empty masks are allowed for glyphs with no visible ink, such as spacing glyphs;
- bearings are bounded;
- rasterization clips to caller-owned target memory;
- coverage is blended into the existing RGBA target without allocating an unbounded intermediate surface;
- malformed or failed providers are explicit errors.

The current paint stage is still opaque-first. This gives ENML real text pixels without coupling typography to live translucency, blur or GPU APIs.

## Resource and power rules

This layer introduces no text worker thread, polling loop or background font scanner. Provider, line-metric and shaping calls are synchronous bounded seams. Future caches must have explicit capacities and invalidation rules and must not turn into permanent background work.

Paragraph shaping should occur because semantic text/layout became dirty, not because an idle timer fired. Animation remains driven by compositor opportunities rather than text-specific timers.

## Security and ownership

Font assets are platform-owned renderer resources. Application semantics choose typography roles, not arbitrary filesystem assets. This prevents public UI ABI from becoming a font-file or native-library capability channel.

Malformed shaping output, invalid fallback use, bad face descriptors, impossible vertical metrics and invalid glyph masks are rejected before they can be trusted by the raster stage.

## Visual-language relationship

Typography is part of ENML's original identity, but the architecture intentionally does not freeze a vendor font or copy another platform's typographic appearance. Display and interface roles may map to coordinated cuts of one future ENML family while preserving international and symbol fallback.

The visual target remains classic, crafted, dimensional and luxurious, but legibility, large-text reflow, accessibility and low-end performance remain first-class constraints.

## Not yet claimed

This milestone does **not** claim that ENML already ships a production Unicode shaper, final font assets, final text hinting, subpixel rendering, color-font support or GPU glyph atlas. The current work establishes the bounded interfaces and real command-to-pixel path those implementations must plug into.
