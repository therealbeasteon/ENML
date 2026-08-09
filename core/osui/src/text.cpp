#include <os/ui/text.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool font_family_valid(FontFamilyRole role) noexcept {
    switch (role) {
    case FontFamilyRole::interface:
    case FontFamilyRole::display:
    case FontFamilyRole::international:
    case FontFamilyRole::symbols:
    case FontFamilyRole::monospace:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool direction_valid(TextDirection direction) noexcept {
    switch (direction) {
    case TextDirection::left_to_right:
    case TextDirection::right_to_left:
        return true;
    }
    return false;
}

[[nodiscard]] bool fallback_valid(const FontFallbackChain& fallback) noexcept {
    if (fallback.count == 0U || fallback.count > fallback.families.size()) return false;
    for (std::size_t index = 0U; index < fallback.count; ++index) {
        if (!font_family_valid(fallback.families[index])) return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (fallback.families[earlier] == fallback.families[index]) return false;
        }
    }
    return true;
}

[[nodiscard]] bool text_style_valid(const ResolvedTextStyle& style) noexcept {
    return style.metrics.size_q6 != 0U &&
        style.metrics.size_q6 <= max_logical_dimension_q6 &&
        style.metrics.line_height_q6 != 0U &&
        style.metrics.line_height_q6 <= max_logical_dimension_q6 &&
        fallback_valid(style.fallback);
}

[[nodiscard]] bool utf8_boundary(const SemanticText& source, std::size_t offset) noexcept {
    const std::size_t length = static_cast<std::size_t>(source.length);
    if (offset == 0U || offset == length) return true;
    if (offset > length) return false;
    const auto byte = static_cast<std::uint8_t>(source.bytes[offset]);
    return (byte & 0xC0U) != 0x80U;
}

[[nodiscard]] bool signed_offset_bounded(std::int32_t value) noexcept {
    const auto widened = static_cast<std::int64_t>(value);
    const auto bound = static_cast<std::int64_t>(max_logical_dimension_q6);
    return widened >= -bound && widened <= bound;
}

} // namespace

bool FontFallbackChain::contains(FontFamilyRole role) const noexcept {
    const std::size_t limit = count < families.size() ? count : families.size();
    for (std::size_t index = 0U; index < limit; ++index) {
        if (families[index] == role) return true;
    }
    return false;
}

os::core::Result<FontFallbackChain> font_fallback_chain(
    TypographyRole role) noexcept {
    FontFallbackChain chain {};

    switch (role) {
    case TypographyRole::body:
    case TypographyRole::label:
        chain.families[0] = FontFamilyRole::interface;
        chain.families[1] = FontFamilyRole::international;
        chain.families[2] = FontFamilyRole::symbols;
        chain.count = 3U;
        return chain;
    case TypographyRole::title:
    case TypographyRole::headline:
        // ENML can have a distinctive platform-owned display face while still
        // falling back to the readable interface/international system stack.
        chain.families[0] = FontFamilyRole::display;
        chain.families[1] = FontFamilyRole::interface;
        chain.families[2] = FontFamilyRole::international;
        chain.families[3] = FontFamilyRole::symbols;
        chain.count = 4U;
        return chain;
    }

    return ui_error(errors::invalid_style);
}

os::core::Result<ResolvedTextStyle> resolve_text_style(
    TypographyRole role,
    std::uint16_t scale_percent) noexcept {
    auto metrics = typography_metrics(role, scale_percent);
    if (!metrics) return metrics.error();

    auto fallback = font_fallback_chain(role);
    if (!fallback) return fallback.error();

    return ResolvedTextStyle{
        .metrics = metrics.value(),
        .fallback = fallback.value(),
    };
}

bool shaped_text_valid(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped) noexcept {
    if (!semantic_text_valid(source) || !text_style_valid(style)) return false;
    if (shaped.glyph_count > shaped.glyphs.size() ||
        shaped.run_count > shaped.runs.size() ||
        shaped.line_count > shaped.lines.size() ||
        shaped.line_height_q6 != style.metrics.line_height_q6) {
        return false;
    }

    if (source.empty()) {
        return shaped.glyph_count == 0U && shaped.run_count == 0U &&
            shaped.line_count == 0U;
    }
    if (shaped.glyph_count == 0U || shaped.run_count == 0U || shaped.line_count == 0U) {
        return false;
    }

    const std::size_t text_length = static_cast<std::size_t>(source.length);
    std::size_t expected_glyph = 0U;
    for (std::size_t run_index = 0U; run_index < shaped.run_count; ++run_index) {
        const ShapedRun& run = shaped.runs[run_index];
        const std::size_t first_glyph = static_cast<std::size_t>(run.first_glyph);
        const std::size_t glyph_count = static_cast<std::size_t>(run.glyph_count);
        const std::size_t text_start = static_cast<std::size_t>(run.text_byte_start);
        const std::size_t text_length_for_run = static_cast<std::size_t>(run.text_byte_length);
        const std::size_t text_end = text_start + text_length_for_run;

        if (glyph_count == 0U || first_glyph != expected_glyph ||
            first_glyph + glyph_count > shaped.glyph_count ||
            text_length_for_run == 0U || text_end > text_length ||
            !utf8_boundary(source, text_start) || !utf8_boundary(source, text_end) ||
            !font_family_valid(run.family) || !style.fallback.contains(run.family) ||
            !direction_valid(run.direction)) {
            return false;
        }

        for (std::size_t glyph_index = first_glyph;
             glyph_index < first_glyph + glyph_count;
             ++glyph_index) {
            const ShapedGlyph& glyph = shaped.glyphs[glyph_index];
            const std::size_t cluster = static_cast<std::size_t>(glyph.cluster_byte_offset);
            if (glyph.family != run.family || cluster < text_start || cluster >= text_end ||
                !utf8_boundary(source, cluster) || glyph.advance_q6 > max_logical_dimension_q6 ||
                !signed_offset_bounded(glyph.offset_x_q6) ||
                !signed_offset_bounded(glyph.offset_y_q6)) {
                return false;
            }
        }
        expected_glyph = first_glyph + glyph_count;
    }
    if (expected_glyph != shaped.glyph_count) return false;

    std::size_t expected_line_glyph = 0U;
    for (std::size_t line_index = 0U; line_index < shaped.line_count; ++line_index) {
        const ShapedLine& line = shaped.lines[line_index];
        const std::size_t first_glyph = static_cast<std::size_t>(line.first_glyph);
        const std::size_t glyph_count = static_cast<std::size_t>(line.glyph_count);
        if (glyph_count == 0U || first_glyph != expected_line_glyph ||
            first_glyph + glyph_count > shaped.glyph_count) {
            return false;
        }
        expected_line_glyph = first_glyph + glyph_count;
    }
    return expected_line_glyph == shaped.glyph_count;
}

os::core::Result<TextMeasurement> measure_shaped_text(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped) noexcept {
    if (!shaped_text_valid(source, style, shaped)) {
        return ui_error(errors::invalid_text_shape);
    }
    if (shaped.line_count == 0U) return TextMeasurement{};

    std::uint64_t maximum_width = 0U;
    for (std::size_t line_index = 0U; line_index < shaped.line_count; ++line_index) {
        const ShapedLine& line = shaped.lines[line_index];
        const std::size_t first_glyph = static_cast<std::size_t>(line.first_glyph);
        const std::size_t glyph_count = static_cast<std::size_t>(line.glyph_count);
        std::uint64_t line_width = 0U;
        for (std::size_t glyph_index = first_glyph;
             glyph_index < first_glyph + glyph_count;
             ++glyph_index) {
            line_width += shaped.glyphs[glyph_index].advance_q6;
            if (line_width > max_logical_dimension_q6) {
                return ui_error(errors::text_shape_limit);
            }
        }
        maximum_width = std::max(maximum_width, line_width);
    }

    const std::uint64_t height =
        static_cast<std::uint64_t>(style.metrics.line_height_q6) *
        static_cast<std::uint64_t>(shaped.line_count);
    if (height > max_logical_dimension_q6 ||
        shaped.line_count > std::numeric_limits<std::uint16_t>::max()) {
        return ui_error(errors::text_shape_limit);
    }

    return TextMeasurement{
        .width_q6 = static_cast<std::uint32_t>(maximum_width),
        .height_q6 = static_cast<std::uint32_t>(height),
        .line_count = static_cast<std::uint16_t>(shaped.line_count),
    };
}

} // namespace os::ui
