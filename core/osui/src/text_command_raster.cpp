#include <os/ui/text_command_raster.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool wrap_valid(ParagraphWrapMode mode) noexcept {
    switch (mode) {
    case ParagraphWrapMode::no_wrap:
    case ParagraphWrapMode::word:
    case ParagraphWrapMode::grapheme:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool overflow_valid(ParagraphOverflowMode mode) noexcept {
    switch (mode) {
    case ParagraphOverflowMode::clip:
    case ParagraphOverflowMode::ellipsis:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool base_direction_valid(ParagraphBaseDirection direction) noexcept {
    switch (direction) {
    case ParagraphBaseDirection::auto_detect:
    case ParagraphBaseDirection::left_to_right:
    case ParagraphBaseDirection::right_to_left:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool policy_valid(const TextCommandRasterPolicy& policy) noexcept {
    return wrap_valid(policy.wrap) && overflow_valid(policy.overflow) &&
        base_direction_valid(policy.base_direction);
}

[[nodiscard]] bool typography_valid(const TypographyMetrics& metrics) noexcept {
    return metrics.size_q6 != 0U && metrics.size_q6 <= max_logical_dimension_q6 &&
        metrics.line_height_q6 != 0U && metrics.line_height_q6 <= max_logical_dimension_q6 &&
        metrics.weight >= 1U && metrics.weight <= 1000U;
}

[[nodiscard]] bool line_metrics_valid(
    const FontLineMetrics& metrics,
    const TypographyMetrics& typography) noexcept {
    if (metrics.ascent_q6 == 0U || metrics.ascent_q6 > max_logical_dimension_q6 ||
        metrics.descent_q6 > max_logical_dimension_q6 ||
        metrics.line_gap_q6 > typography.line_height_q6) {
        return false;
    }
    const std::uint64_t ink_height =
        static_cast<std::uint64_t>(metrics.ascent_q6) + metrics.descent_q6;
    return ink_height <= typography.line_height_q6;
}

[[nodiscard]] os::core::Result<FontLineMetrics> resolve_one_line_metrics(
    const FontFaceDescriptor& face,
    const ResolvedTextStyle& style,
    FontLineMetricsBackend backend) noexcept {
    FontLineMetrics resolved {};
    if (!backend.resolve(backend.context, face, style.metrics, resolved)) {
        return ui_error(errors::font_line_metrics_failed);
    }
    if (!line_metrics_valid(resolved, style.metrics)) {
        return ui_error(errors::invalid_font_line_metrics);
    }
    return resolved;
}

[[nodiscard]] os::core::Result<FontLineMetrics> paragraph_line_metrics(
    const ResolvedTextStyle& style,
    const FontFaceSet& faces,
    const ShapedText& shaped,
    FontLineMetricsBackend backend) noexcept {
    if (backend.resolve == nullptr) {
        return ui_error(errors::font_line_metrics_unavailable);
    }

    FontLineMetrics combined {};
    bool any = false;
    for (std::size_t glyph_index = 0U; glyph_index < shaped.glyph_count; ++glyph_index) {
        const FontFamilyRole family = shaped.glyphs[glyph_index].family;
        bool seen = false;
        for (std::size_t earlier = 0U; earlier < glyph_index; ++earlier) {
            if (shaped.glyphs[earlier].family == family) {
                seen = true;
                break;
            }
        }
        if (seen) continue;

        const FontFaceDescriptor* face = faces.find(family);
        if (face == nullptr) return ui_error(errors::invalid_font_face);
        auto resolved = resolve_one_line_metrics(*face, style, backend);
        if (!resolved) return resolved.error();

        combined.ascent_q6 = std::max(combined.ascent_q6, resolved.value().ascent_q6);
        combined.descent_q6 = std::max(combined.descent_q6, resolved.value().descent_q6);
        combined.line_gap_q6 = std::max(combined.line_gap_q6, resolved.value().line_gap_q6);
        any = true;
    }

    // A paragraph containing only explicit hard separators legitimately has no
    // glyphs but still owns line boxes. Resolve those boxes from the first
    // semantic fallback face rather than inventing geometry or rejecting the
    // blank paragraph after the shaping contract has already validated it.
    if (!any) {
        if (faces.count == 0U) return ui_error(errors::invalid_font_face);
        return resolve_one_line_metrics(faces.faces[0], style, backend);
    }

    if (!line_metrics_valid(combined, style.metrics)) {
        return ui_error(errors::invalid_font_line_metrics);
    }
    return combined;
}

[[nodiscard]] std::uint8_t max_lines_for_bounds(
    const LogicalRect& bounds,
    const TypographyMetrics& typography) noexcept {
    if (typography.line_height_q6 == 0U) return 0U;
    std::uint32_t count = bounds.height_q6 / typography.line_height_q6;
    if (count == 0U) count = 1U;
    if (count > max_shaped_lines) count = static_cast<std::uint32_t>(max_shaped_lines);
    return static_cast<std::uint8_t>(count);
}

[[nodiscard]] os::core::Result<TextRasterOrigin> text_origin(
    const LogicalRect& bounds,
    const TypographyMetrics& typography,
    const FontLineMetrics& metrics) noexcept {
    const std::uint64_t ink_height =
        static_cast<std::uint64_t>(metrics.ascent_q6) + metrics.descent_q6;
    if (ink_height > typography.line_height_q6) {
        return ui_error(errors::invalid_font_line_metrics);
    }
    const std::uint32_t leading_q6 =
        typography.line_height_q6 - static_cast<std::uint32_t>(ink_height);
    const std::uint32_t leading_before_q6 = leading_q6 / 2U;

    const std::int64_t baseline_y =
        static_cast<std::int64_t>(bounds.y_q6) +
        static_cast<std::int64_t>(leading_before_q6) +
        static_cast<std::int64_t>(metrics.ascent_q6);
    const std::int64_t bound = static_cast<std::int64_t>(max_logical_dimension_q6);
    if (baseline_y < -bound || baseline_y > bound) {
        return ui_error(errors::invalid_raster_command);
    }

    return TextRasterOrigin{
        .baseline_x_q6 = bounds.x_q6,
        .first_baseline_y_q6 = static_cast<std::int32_t>(baseline_y),
    };
}

} // namespace

os::core::Result<TextCommandRasterStats> rasterize_render_command_text(
    const RenderCommandBuffer& commands,
    TextCommandRasterBackend backend,
    const RasterTheme& theme,
    RasterTarget target,
    TextCommandRasterPolicy policy) noexcept {
    if (commands.count > commands.commands.size() || !policy_valid(policy)) {
        return ui_error(errors::invalid_raster_command);
    }

    TextCommandRasterStats stats {};
    for (std::size_t index = 0U; index < commands.count; ++index) {
        const RenderCommand& command = commands.commands[index];
        ++stats.commands_seen;
        if (command.content != RenderContentKind::text) continue;
        ++stats.text_commands_seen;

        if (command.role != UiRole::text || !command.bounds.bounded() ||
            !typography_valid(command.typography) ||
            !semantic_text_valid(command.visual_text)) {
            return ui_error(errors::invalid_raster_command);
        }
        if (command.visual_text.empty()) continue;

        const ResolvedTextStyle style{
            .metrics = command.typography,
            .fallback = command.font_fallbacks,
        };
        auto faces = resolve_font_faces(style.fallback, backend.fonts);
        if (!faces) return faces.error();

        const std::uint8_t max_lines = max_lines_for_bounds(command.bounds, command.typography);
        if (max_lines == 0U) return ui_error(errors::invalid_raster_command);
        const ParagraphConstraints constraints{
            .max_width_q6 = command.bounds.width_q6,
            .max_lines = max_lines,
            .wrap = policy.wrap,
            .overflow = policy.overflow,
            .base_direction = policy.base_direction,
        };
        auto shaped = shape_paragraph_with_fonts(
            command.visual_text,
            style,
            faces.value(),
            constraints,
            backend.paragraphs);
        if (!shaped) return shaped.error();
        ++stats.paragraphs_shaped;

        auto metrics = paragraph_line_metrics(
            style,
            faces.value(),
            shaped.value(),
            backend.line_metrics);
        if (!metrics) return metrics.error();
        auto origin = text_origin(command.bounds, command.typography, metrics.value());
        if (!origin) return origin.error();

        // The semantic/layout node owns a bounded paint rectangle. Font ink may
        // legitimately overhang its advance box, but it must not overwrite a
        // sibling merely because a backend glyph bearing extends outside this
        // command. Target clipping still applies underneath this logical clip.
        auto raster = rasterize_shaped_text_masks_clipped(
            command.visual_text,
            style,
            shaped.value(),
            faces.value(),
            backend.glyphs,
            theme,
            command.visual.token.foreground,
            origin.value(),
            command.bounds,
            target);
        if (!raster) return raster.error();

        stats.glyphs_seen += raster.value().glyphs_seen;
        stats.glyphs_drawn += raster.value().glyphs_drawn;
        if (stats.pixel_writes >
            std::numeric_limits<std::uint64_t>::max() - raster.value().pixel_writes) {
            return ui_error(errors::text_shape_limit);
        }
        stats.pixel_writes += raster.value().pixel_writes;
    }

    return stats;
}

} // namespace os::ui
