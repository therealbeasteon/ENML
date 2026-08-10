#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/glyph_raster.hpp>
#include <os/ui/paragraph.hpp>
#include <os/ui/renderer.hpp>

namespace os::ui {

// Vertical metrics are resolved by the renderer-owned font backend at the
// semantic typography size. Keeping them separate from public font identity
// avoids exposing filesystem/native font details while still letting text be
// positioned from actual font metrics rather than guessed percentages.
struct FontLineMetrics final {
    std::uint32_t ascent_q6 {0U};
    std::uint32_t descent_q6 {0U};
    std::uint32_t line_gap_q6 {0U};
};

using ResolveFontLineMetricsBackendFn = bool (*)(
    void* context,
    const FontFaceDescriptor& face,
    const TypographyMetrics& typography,
    FontLineMetrics& output) noexcept;

struct FontLineMetricsBackend final {
    void* context {nullptr};
    ResolveFontLineMetricsBackendFn resolve {nullptr};
};

struct TextCommandRasterBackend final {
    FontProviderBackend fonts {};
    FontAwareParagraphShaperBackend paragraphs {};
    FontLineMetricsBackend line_metrics {};
    GlyphMaskProviderBackend glyphs {};
};

struct TextCommandRasterPolicy final {
    ParagraphWrapMode wrap {ParagraphWrapMode::word};
    ParagraphOverflowMode overflow {ParagraphOverflowMode::clip};
    ParagraphBaseDirection base_direction {ParagraphBaseDirection::auto_detect};
};

struct TextCommandRasterStats final {
    std::uint16_t commands_seen {0U};
    std::uint16_t text_commands_seen {0U};
    std::uint16_t paragraphs_shaped {0U};
    std::uint32_t glyphs_seen {0U};
    std::uint32_t glyphs_drawn {0U};
    std::uint64_t pixel_writes {0U};
};

// Paints visible RenderContentKind::text commands through the already-bounded
// font -> paragraph shaping -> glyph coverage pipeline. The command's logical
// bounds provide paragraph width and clipping budget; baseline placement comes
// from real renderer-owned font vertical metrics. Applications still never
// submit glyph IDs, font files, masks, or native shaping handles.
[[nodiscard]] os::core::Result<TextCommandRasterStats> rasterize_render_command_text(
    const RenderCommandBuffer& commands,
    TextCommandRasterBackend backend,
    const RasterTheme& theme,
    RasterTarget target,
    TextCommandRasterPolicy policy = {}) noexcept;

} // namespace os::ui
