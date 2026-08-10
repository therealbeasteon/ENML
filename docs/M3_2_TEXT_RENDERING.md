# M3.2 — bounded text rendering path

This note records the current renderer-private text path and the limits that keep it aligned with ENML's product vision.

## Implementation authority

References teach principles; ENML determines implementation. The Linux text adapter uses private third-party implementation libraries because they satisfy ENML's bounded renderer seams, not because their APIs define ENML's public text model. Applications see ENML semantic typography, text, bounds and error contracts only.

## Pipeline

The current CPU/economy path is:

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
→ command-owned semantic paint clip
→ `RenderCommandBuffer` text placement
→ ENML raster target

Applications do not submit font paths, vendor family names, native font-library handles, glyph IDs, glyph bitmaps, baseline positions or font metric tables. Those remain renderer implementation details.

## Production Linux adapter

`core/osui/platform/linux` provides a concrete `LinuxTextBackend` behind the existing ENML interfaces.

Its private implementation uses:

- FreeType for configured platform font-face loading, vertical metrics and grayscale glyph masks;
- HarfBuzz for OpenType shaping and cluster/glyph positioning;
- ICU for Unicode bidi resolution plus line/grapheme break analysis.

Those libraries remain replaceable implementation dependencies. They do not define the application ABI, semantic roles, collection/layout model, wire protocols or ENML visual language.

The adapter owns a fixed five-role face table (`interface`, `display`, `international`, `symbols`, `monospace`), fixed-capacity UTF-16/break/segment scratch bounded by `SemanticText`, pre-opened ICU analysis objects and one reusable renderer-owned HarfBuzz buffer. It does not introduce a font scanner, per-text worker, polling loop or unbounded serialized paragraph.

A product image supplies renderer-private font asset paths through `LinuxFontFiles`. Missing/invalid configured assets make the backend invalid and expose no usable callbacks; applications do not fall back to arbitrary filesystem fonts.

The current adapter supports real Latin shaping, Unicode line wrapping, mixed LTR/RTL visual runs, renderer-owned line metrics and grayscale FreeType masks in the four-runner M3 UI validation matrix. Ellipsis remains fail-closed when truncation would require a synthetic glyph/source-cluster contract that ENML has not yet defined.

## Grapheme-safe fallback ownership

Font fallback is an ENML renderer rule, not a side effect of a particular native library's scalar lookup behavior.

Fallback family selection now occurs at Unicode grapheme boundaries. The adapter chooses one configured semantic face for the complete grapheme and only merges adjacent complete graphemes when they resolve to the same family. A base character and its combining marks, variation selectors or shaping controls therefore cannot be split into unrelated font runs merely because scalar-by-scalar coverage differs across configured faces.

Default-ignorable shaping scalars are kept inside the grapheme handed to the shaper but do not independently force a fallback-family switch when they have no standalone glyph. If no configured face covers every glyph-bearing scalar in a grapheme, ENML deliberately keeps the grapheme intact and selects the ordinary fallback for its first glyph-bearing scalar. A renderer may then expose a missing glyph, but it does not corrupt one user-perceived character into cross-face fragments.

The production Linux backend regression test includes a base character plus combining acute accent and proves that neither a run boundary nor a shaped cluster is introduced at the combining-mark byte offset. This is a stronger contract than merely checking that UTF-8 byte boundaries are valid.

## Unicode and paragraph layout

ENML does not implement a partial home-grown bidi or script-shaping algorithm in the core UI layer. `FontAwareParagraphShaperBackend` is the bounded integration boundary.

The platform passes explicit width and line limits plus wrap/overflow/base-direction intent. Backend output is accepted only when it satisfies the existing UTF-8, run, glyph, fallback-family and shaped-line invariants and remains within the requested paragraph bounds.

Each shaped line is normalized into renderer paint order by the backend adapter. `TextDirection` remains semantic metadata rather than an excuse for application-controlled positioning.

Word wrapping uses Unicode line-break opportunities. An overlong token can fall back to Unicode grapheme boundaries as an emergency fit mechanism rather than splitting an arbitrary UTF-8 byte/surrogate position. If even the smallest complete grapheme cannot fit, the bounded core validator rejects the result instead of silently losing text.

Empty semantic text is a valid zero-glyph/zero-line no-paint result. Explicit hard separators own line layout even when a line contains no glyphs: consecutive hard separators preserve bounded blank visual lines without manufacturing fake source characters or glyph records.

## Renderer-command text integration

`rasterize_render_command_text()` consumes visible `RenderContentKind::text` commands directly. It does not create a second application-owned text display list.

For each bounded text command the renderer:

1. uses the command bounds as the paragraph width/height budget;
2. resolves only the semantic fallback families already attached to the command;
3. shapes through the renderer-private paragraph backend;
4. asks the renderer-private font backend for vertical line metrics at the semantic typography size;
5. derives the first baseline from actual ascent/descent plus bounded leading rather than a guessed fixed percentage;
6. paints validated glyph masks in the command's semantic foreground color;
7. clips every resulting pixel to the command's logical rectangle as well as the final raster target.

The final clipping step is important because real font ink may overhang its advance origin through negative/positive bearings. Such overhang is legitimate font behavior but is not authority to paint into a sibling semantic node. ENML therefore treats node bounds as a paint-ownership boundary for this CPU path without allocating an offscreen text surface.

When several fallback families appear in one paragraph, baseline placement uses the maximum validated ascent/descent required by the shaped glyph families. A malformed font whose ink metrics cannot fit the semantic line box is rejected rather than silently overlapping adjacent lines.

`rasterize_opaque_frame_with_text()` composes the existing opaque material/contour path and this text path in one deterministic frame entry point. Geometry is painted first and text is painted afterward. The Linux backend is exercised through this complete command-to-pixel path in CI rather than only through isolated shaping and mask calls.

## Glyph raster boundary

`GlyphMaskProviderBackend` receives only a validated opaque `FontFaceDescriptor`, semantic typography metrics, one renderer-private glyph ID and the target raster scale. It returns a transient coverage view consumed synchronously.

Current limits:

- glyph masks are at most 512 × 512 pixels;
- mask stride and byte capacity are validated before use;
- empty masks are allowed for glyphs with no visible ink, such as spacing glyphs;
- bearings are bounded;
- raw glyph rasterization clips to caller-owned target memory;
- render-command text additionally clips to the semantic command rectangle;
- coverage is blended into the existing RGBA target without allocating an unbounded intermediate surface;
- malformed or failed providers are explicit errors.

The current paint stage is still opaque-first. This gives ENML real text pixels without coupling typography to live translucency, blur or GPU APIs.

## Resource and power rules

This layer introduces no text worker thread, polling loop or background font scanner. Provider, line-metric and shaping calls are synchronous bounded seams. Future caches must have explicit capacities and invalidation rules and must not turn into permanent background work.

Paragraph shaping should occur because semantic text/layout became dirty, not because an idle timer fired. Animation remains driven by compositor opportunities rather than text-specific timers.

The Linux backend mutates its private font/analysis objects during shaping/rasterization and is therefore renderer-owned serialized state. Its reusable HarfBuzz buffer removes per-run buffer construction/destruction from the hot path without creating shared global mutable state. Any future parallel renderer must either provide per-worker bounded backend state or explicit serialization after measurement shows that parallelism is justified.

## Security and ownership

Font assets are platform-owned renderer resources. Application semantics choose typography roles, not arbitrary filesystem assets. This prevents public UI ABI from becoming a font-file or native-library capability channel.

Malformed shaping output, invalid fallback use, bad face descriptors, impossible vertical metrics, invalid glyph masks and invalid command paint bounds are rejected before they can be trusted by the raster stage.

## Visual-language relationship

Typography is part of ENML's original identity, but the architecture intentionally does not freeze a vendor font or copy another platform's typographic appearance. Display and interface roles may map to coordinated cuts of one future ENML family while preserving international and symbol fallback.

The visual target remains classic, crafted, dimensional and luxurious, but legibility, large-text reflow, accessibility and low-end performance remain first-class constraints.

## Current claim boundary

M3.2 has a concrete production-oriented Linux Unicode/font backend and a real semantic-command-to-pixel path. That does **not** mean the final ENML font product is finished.

Still intentionally outside the current claim:

- final ENML platform font assets and licensing/package policy;
- a finalized synthetic-ellipsis/source-cluster contract;
- editable-text/IME shaping semantics;
- final hinting policy across target display densities;
- color-font/emoji policy;
- GPU glyph atlas/cache implementation;
- hardware compositor/GPU text path.

Those later implementations must preserve the bounded semantic/provider contracts established here rather than exposing native font APIs to applications.
