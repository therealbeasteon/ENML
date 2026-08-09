# M3.2 — bounded text rendering path

This note records the current renderer-private text path and the limits that keep it aligned with ENML's product vision.

## Pipeline

The intended production path is:

`SemanticText + TypographyRole`
→ `ResolvedTextStyle`
→ semantic `FontFallbackChain`
→ renderer-owned `FontProviderBackend`
→ opaque `FontFaceSet`
→ renderer-owned paragraph/shaping backend
→ validated `ShapedText`
→ renderer-owned `GlyphMaskProviderBackend`
→ bounded glyph coverage
→ ENML raster target

Applications do not submit font paths, vendor family names, native font-library handles, glyph IDs or glyph bitmaps. Those remain renderer implementation details.

## Unicode and paragraph layout

ENML does not implement a partial home-grown bidi or script-shaping algorithm in the core UI layer. `FontAwareParagraphShaperBackend` is the integration boundary for a reviewed production text engine.

The platform passes explicit width and line limits plus wrap/overflow/base-direction intent. Backend output is accepted only when it satisfies the existing UTF-8, run, glyph, fallback-family and shaped-line invariants and remains within the requested paragraph bounds.

Each shaped line is normalized into renderer paint order by the backend adapter. `TextDirection` remains semantic metadata rather than an excuse for application-controlled positioning.

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

This layer introduces no text worker thread, polling loop or background font scanner. Provider and shaping calls are synchronous bounded seams. Future caches must have explicit capacities and invalidation rules and must not turn into permanent background work.

Paragraph shaping should occur because semantic text/layout became dirty, not because an idle timer fired. Animation remains driven by compositor opportunities rather than text-specific timers.

## Security and ownership

Font assets are platform-owned renderer resources. Application semantics choose typography roles, not arbitrary filesystem assets. This prevents public UI ABI from becoming a font-file or native-library capability channel.

Malformed shaping output, invalid fallback use, bad face descriptors and invalid glyph masks are rejected before they can be trusted by the raster stage.

## Visual-language relationship

Typography is part of ENML's original identity, but the architecture intentionally does not freeze a vendor font or copy another platform's typographic appearance. Display and interface roles may map to coordinated cuts of one future ENML family while preserving international and symbol fallback.

The visual target remains classic, crafted, dimensional and luxurious, but legibility, large-text reflow, accessibility and low-end performance remain first-class constraints.

## Not yet claimed

This milestone does **not** claim that ENML already ships a production Unicode shaper, final font assets, final text hinting, subpixel rendering, color-font support or GPU glyph atlas. The current work establishes the bounded interfaces and real coverage-to-pixel path those implementations must plug into.
