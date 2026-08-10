#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/raster.hpp>
#include <os/ui/text.hpp>

namespace os::ui {

// Renderer-private glyph coverage supplied by a production font backend.
// The view is transient and consumed synchronously; applications never submit
// masks, glyph IDs, font files or native font handles through public UI APIs.
struct GlyphMaskView final {
    const std::uint8_t* coverage {nullptr};
    std::size_t byte_count {0U};
    std::uint16_t width {0U};
    std::uint16_t height {0U};
    std::uint16_t stride {0U};
    std::int16_t bearing_x_px {0};
    std::int16_t bearing_top_px {0};
};

inline constexpr std::uint16_t max_glyph_mask_dimension = 512U;

using ResolveGlyphMaskBackendFn = bool (*)(
    void* context,
    const FontFaceDescriptor& face,
    const TypographyMetrics& metrics,
    std::uint32_t glyph_id,
    RasterScale scale,
    GlyphMaskView& output) noexcept;

struct GlyphMaskProviderBackend final {
    void* context {nullptr};
    ResolveGlyphMaskBackendFn resolve {nullptr};
};

// The origin is the baseline of the first shaped line. Subsequent line
// baselines advance by the validated semantic line height. Shaping adapters
// normalize each ShapedLine into left-to-right paint order; TextDirection
// remains semantic metadata used by the shaping/layout backend.
struct TextRasterOrigin final {
    std::int32_t baseline_x_q6 {0};
    std::int32_t first_baseline_y_q6 {0};
};

struct GlyphRasterStats final {
    std::uint16_t glyphs_seen {0U};
    std::uint16_t masks_resolved {0U};
    std::uint16_t glyphs_drawn {0U};
    std::uint64_t pixel_writes {0U};
};

// Opaque-first text paint stage. It consumes already-validated shaped text,
// renderer-owned opaque font face IDs and transient backend coverage masks.
// Coverage is blended against the existing target pixels; this deliberately
// does not introduce a public font ABI or a hidden text worker/cache policy.
[[nodiscard]] os::core::Result<GlyphRasterStats> rasterize_shaped_text_masks(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped,
    const FontFaceSet& faces,
    GlyphMaskProviderBackend provider,
    const RasterTheme& theme,
    ColorRole color,
    TextRasterOrigin origin,
    RasterTarget target) noexcept;

// Same bounded glyph-mask path, but pixels are additionally clipped to a
// validated logical rectangle before touching the caller-owned target. This is
// the path used by RenderContentKind::text so font overhang/bearings cannot
// paint into sibling UI regions merely because a glyph mask extends beyond the
// semantic node's layout box. The clip is renderer policy, not application font
// control; target clipping remains in force as a second bound.
[[nodiscard]] os::core::Result<GlyphRasterStats> rasterize_shaped_text_masks_clipped(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped,
    const FontFaceSet& faces,
    GlyphMaskProviderBackend provider,
    const RasterTheme& theme,
    ColorRole color,
    TextRasterOrigin origin,
    LogicalRect clip_bounds,
    RasterTarget target) noexcept;

} // namespace os::ui
