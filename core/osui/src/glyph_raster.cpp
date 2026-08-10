#include <os/ui/glyph_raster.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool color_role_valid(ColorRole role) noexcept {
    switch (role) {
    case ColorRole::transparent:
    case ColorRole::surface:
    case ColorRole::surface_elevated:
    case ColorRole::text_primary:
    case ColorRole::text_secondary:
    case ColorRole::accent:
    case ColorRole::on_accent:
    case ColorRole::outline:
    case ColorRole::focus:
    case ColorRole::critical:
    case ColorRole::on_critical:
    case ColorRole::accent_secondary:
    case ColorRole::accent_tertiary:
    case ColorRole::highlight:
        return true;
    }
    return false;
}

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

[[nodiscard]] constexpr std::size_t color_index(ColorRole role) noexcept {
    return static_cast<std::size_t>(role);
}

[[nodiscard]] bool theme_valid(const RasterTheme& theme) noexcept {
    if (theme.colors[color_index(ColorRole::transparent)].alpha != 0U) return false;
    for (std::size_t index = 1U; index < theme.colors.size(); ++index) {
        if (theme.colors[index].alpha != 255U) return false;
    }
    return true;
}

[[nodiscard]] bool target_valid(const RasterTarget& target) noexcept {
    if (target.pixels == nullptr || target.width == 0U || target.height == 0U ||
        target.width > max_raster_dimension || target.height > max_raster_dimension ||
        target.stride < target.width || target.stride > max_raster_dimension ||
        target.scale.numerator == 0U || target.scale.numerator > 16U ||
        target.scale.denominator == 0U || target.scale.denominator > 1024U) {
        return false;
    }
    const std::size_t required =
        static_cast<std::size_t>(target.stride) * static_cast<std::size_t>(target.height);
    return target.pixel_count >= required;
}

[[nodiscard]] bool face_valid(const FontFaceDescriptor& face) noexcept {
    return face.id.value() != 0U && font_family_valid(face.family) &&
        face.units_per_em >= 16U && face.units_per_em <= 16384U &&
        face.weight_min >= 1U && face.weight_min <= 1000U &&
        face.weight_max >= 1U && face.weight_max <= 1000U &&
        face.weight_min <= face.weight_max;
}

[[nodiscard]] bool face_set_valid(
    const FontFallbackChain& fallback,
    const FontFaceSet& faces) noexcept {
    if (fallback.count == 0U || fallback.count > fallback.families.size() ||
        faces.count != fallback.count || faces.count > faces.faces.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < faces.count; ++index) {
        if (!face_valid(faces.faces[index]) ||
            faces.faces[index].family != fallback.families[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool mask_valid(const GlyphMaskView& mask) noexcept {
    if (mask.width == 0U || mask.height == 0U) {
        return mask.width == 0U && mask.height == 0U && mask.stride == 0U &&
            mask.byte_count == 0U && mask.coverage == nullptr;
    }
    if (mask.coverage == nullptr || mask.width > max_glyph_mask_dimension ||
        mask.height > max_glyph_mask_dimension || mask.stride < mask.width ||
        mask.stride > max_glyph_mask_dimension) {
        return false;
    }
    const std::size_t required =
        static_cast<std::size_t>(mask.stride) * static_cast<std::size_t>(mask.height);
    if (mask.byte_count < required) return false;
    const auto bearing_limit = static_cast<std::int32_t>(max_glyph_mask_dimension);
    return static_cast<std::int32_t>(mask.bearing_x_px) >= -bearing_limit &&
        static_cast<std::int32_t>(mask.bearing_x_px) <= bearing_limit &&
        static_cast<std::int32_t>(mask.bearing_top_px) >= -bearing_limit &&
        static_cast<std::int32_t>(mask.bearing_top_px) <= bearing_limit;
}

[[nodiscard]] constexpr std::int64_t floor_div(
    std::int64_t value,
    std::int64_t divisor) noexcept {
    if (value >= 0) return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

[[nodiscard]] constexpr std::int64_t ceil_div(
    std::int64_t value,
    std::int64_t divisor) noexcept {
    if (value >= 0) return (value + divisor - 1) / divisor;
    return -((-value) / divisor);
}

[[nodiscard]] std::int64_t scale_floor(
    std::int64_t q6,
    const RasterScale& scale) noexcept {
    return floor_div(
        q6 * static_cast<std::int64_t>(scale.numerator),
        static_cast<std::int64_t>(scale.denominator));
}

[[nodiscard]] std::int64_t scale_ceil(
    std::int64_t q6,
    const RasterScale& scale) noexcept {
    return ceil_div(
        q6 * static_cast<std::int64_t>(scale.numerator),
        static_cast<std::int64_t>(scale.denominator));
}

[[nodiscard]] constexpr std::uint8_t blend_channel_coverage(
    std::uint8_t destination,
    std::uint8_t source,
    std::uint8_t coverage) noexcept {
    const std::uint32_t inverse = 255U - coverage;
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(destination) * inverse +
         static_cast<std::uint32_t>(source) * coverage + 127U) /
        255U);
}

[[nodiscard]] constexpr Rgba8 blend_coverage(
    Rgba8 destination,
    Rgba8 source,
    std::uint8_t coverage) noexcept {
    return Rgba8{
        .red = blend_channel_coverage(destination.red, source.red, coverage),
        .green = blend_channel_coverage(destination.green, source.green, coverage),
        .blue = blend_channel_coverage(destination.blue, source.blue, coverage),
        .alpha = static_cast<std::uint8_t>(
            coverage +
            (static_cast<std::uint32_t>(destination.alpha) * (255U - coverage) + 127U) /
                255U),
    };
}

[[nodiscard]] std::size_t pixel_index(
    const RasterTarget& target,
    std::uint32_t x,
    std::uint32_t y) noexcept {
    return static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(x);
}

[[nodiscard]] bool origin_valid(TextRasterOrigin origin) noexcept {
    const auto bound = static_cast<std::int64_t>(max_logical_dimension_q6);
    return static_cast<std::int64_t>(origin.baseline_x_q6) >= -bound &&
        static_cast<std::int64_t>(origin.baseline_x_q6) <= bound &&
        static_cast<std::int64_t>(origin.first_baseline_y_q6) >= -bound &&
        static_cast<std::int64_t>(origin.first_baseline_y_q6) <= bound;
}

struct PixelClip final {
    std::int64_t left {0};
    std::int64_t top {0};
    std::int64_t right {0};
    std::int64_t bottom {0};
};

[[nodiscard]] PixelClip full_target_clip(const RasterTarget& target) noexcept {
    return PixelClip{
        .left = 0,
        .top = 0,
        .right = static_cast<std::int64_t>(target.width),
        .bottom = static_cast<std::int64_t>(target.height),
    };
}

[[nodiscard]] PixelClip logical_clip(
    const LogicalRect& bounds,
    const RasterTarget& target) noexcept {
    const std::int64_t right_q6 =
        static_cast<std::int64_t>(bounds.x_q6) + bounds.width_q6;
    const std::int64_t bottom_q6 =
        static_cast<std::int64_t>(bounds.y_q6) + bounds.height_q6;
    return PixelClip{
        .left = std::max<std::int64_t>(0, scale_floor(bounds.x_q6, target.scale)),
        .top = std::max<std::int64_t>(0, scale_floor(bounds.y_q6, target.scale)),
        .right = std::min<std::int64_t>(
            target.width, scale_ceil(right_q6, target.scale)),
        .bottom = std::min<std::int64_t>(
            target.height, scale_ceil(bottom_q6, target.scale)),
    };
}

[[nodiscard]] bool inside_clip(
    const PixelClip& clip,
    std::int64_t x,
    std::int64_t y) noexcept {
    return x >= clip.left && x < clip.right && y >= clip.top && y < clip.bottom;
}

[[nodiscard]] os::core::Result<GlyphRasterStats> rasterize_masks_impl(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped,
    const FontFaceSet& faces,
    GlyphMaskProviderBackend provider,
    const RasterTheme& theme,
    ColorRole color,
    TextRasterOrigin origin,
    const PixelClip& clip,
    RasterTarget target) noexcept {
    if (!target_valid(target)) return ui_error(errors::invalid_raster_target);
    if (!theme_valid(theme) || !color_role_valid(color)) {
        return ui_error(errors::invalid_raster_theme);
    }
    if (!origin_valid(origin) || !shaped_text_valid(source, style, shaped)) {
        return ui_error(errors::invalid_text_shape);
    }
    if (!face_set_valid(style.fallback, faces)) {
        return ui_error(errors::invalid_font_face);
    }
    if (provider.resolve == nullptr) {
        return ui_error(errors::glyph_provider_unavailable);
    }

    auto measurement = measure_shaped_text(source, style, shaped);
    if (!measurement) return measurement.error();

    GlyphRasterStats stats {};
    const Rgba8 foreground = theme.colors[color_index(color)];
    if (foreground.alpha == 0U || shaped.glyph_count == 0U ||
        clip.left >= clip.right || clip.top >= clip.bottom) {
        return stats;
    }

    for (std::size_t line_index = 0U; line_index < shaped.line_count; ++line_index) {
        const ShapedLine& line = shaped.lines[line_index];
        const std::size_t first_glyph = static_cast<std::size_t>(line.first_glyph);
        const std::size_t glyph_count = static_cast<std::size_t>(line.glyph_count);
        const std::int64_t baseline_y_q6 =
            static_cast<std::int64_t>(origin.first_baseline_y_q6) +
            static_cast<std::int64_t>(line_index) * style.metrics.line_height_q6;
        std::int64_t pen_x_q6 = origin.baseline_x_q6;

        for (std::size_t glyph_index = first_glyph;
             glyph_index < first_glyph + glyph_count;
             ++glyph_index) {
            const ShapedGlyph& glyph = shaped.glyphs[glyph_index];
            if (stats.glyphs_seen == std::numeric_limits<std::uint16_t>::max()) {
                return ui_error(errors::text_shape_limit);
            }
            ++stats.glyphs_seen;

            const FontFaceDescriptor* face = faces.find(glyph.family);
            if (face == nullptr || !face_valid(*face)) {
                return ui_error(errors::invalid_font_face);
            }

            GlyphMaskView mask {};
            if (!provider.resolve(
                    provider.context,
                    *face,
                    style.metrics,
                    glyph.glyph_id,
                    target.scale,
                    mask)) {
                return ui_error(errors::glyph_provider_failed);
            }
            if (!mask_valid(mask)) return ui_error(errors::invalid_glyph_mask);
            ++stats.masks_resolved;

            bool glyph_drawn = false;
            if (mask.width != 0U && mask.height != 0U) {
                const std::int64_t glyph_x_q6 =
                    pen_x_q6 + static_cast<std::int64_t>(glyph.offset_x_q6);
                const std::int64_t glyph_baseline_q6 =
                    baseline_y_q6 + static_cast<std::int64_t>(glyph.offset_y_q6);
                const std::int64_t left =
                    scale_floor(glyph_x_q6, target.scale) + mask.bearing_x_px;
                const std::int64_t top =
                    scale_floor(glyph_baseline_q6, target.scale) - mask.bearing_top_px;

                for (std::uint32_t mask_y = 0U; mask_y < mask.height; ++mask_y) {
                    const std::int64_t target_y = top + static_cast<std::int64_t>(mask_y);
                    if (target_y < 0 || target_y >= static_cast<std::int64_t>(target.height) ||
                        target_y < clip.top || target_y >= clip.bottom) {
                        continue;
                    }
                    for (std::uint32_t mask_x = 0U; mask_x < mask.width; ++mask_x) {
                        const std::int64_t target_x = left + static_cast<std::int64_t>(mask_x);
                        if (target_x < 0 || target_x >= static_cast<std::int64_t>(target.width) ||
                            !inside_clip(clip, target_x, target_y)) {
                            continue;
                        }
                        const std::size_t mask_index =
                            static_cast<std::size_t>(mask_y) * mask.stride + mask_x;
                        const std::uint8_t coverage = mask.coverage[mask_index];
                        if (coverage == 0U) continue;

                        const auto ux = static_cast<std::uint32_t>(target_x);
                        const auto uy = static_cast<std::uint32_t>(target_y);
                        const std::size_t destination_index = pixel_index(target, ux, uy);
                        target.pixels[destination_index] = blend_coverage(
                            target.pixels[destination_index],
                            foreground,
                            coverage);
                        ++stats.pixel_writes;
                        glyph_drawn = true;
                    }
                }
            }
            if (glyph_drawn) ++stats.glyphs_drawn;
            pen_x_q6 += glyph.advance_q6;
        }
    }

    return stats;
}

} // namespace

os::core::Result<GlyphRasterStats> rasterize_shaped_text_masks(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped,
    const FontFaceSet& faces,
    GlyphMaskProviderBackend provider,
    const RasterTheme& theme,
    ColorRole color,
    TextRasterOrigin origin,
    RasterTarget target) noexcept {
    if (!target_valid(target)) return ui_error(errors::invalid_raster_target);
    return rasterize_masks_impl(
        source,
        style,
        shaped,
        faces,
        provider,
        theme,
        color,
        origin,
        full_target_clip(target),
        target);
}

os::core::Result<GlyphRasterStats> rasterize_shaped_text_masks_clipped(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped,
    const FontFaceSet& faces,
    GlyphMaskProviderBackend provider,
    const RasterTheme& theme,
    ColorRole color,
    TextRasterOrigin origin,
    LogicalRect clip_bounds,
    RasterTarget target) noexcept {
    if (!target_valid(target)) return ui_error(errors::invalid_raster_target);
    if (!clip_bounds.bounded()) return ui_error(errors::invalid_raster_command);
    return rasterize_masks_impl(
        source,
        style,
        shaped,
        faces,
        provider,
        theme,
        color,
        origin,
        logical_clip(clip_bounds, target),
        target);
}

} // namespace os::ui
