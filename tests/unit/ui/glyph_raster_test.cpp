#include <os/ui/glyph_raster.hpp>
#include <os/ui/types.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

constexpr std::size_t color_index(os::ui::ColorRole role) {
    return static_cast<std::size_t>(role);
}

os::ui::RasterTheme test_theme() {
    os::ui::RasterTheme theme{};
    for (auto& color : theme.colors) color = os::ui::Rgba8{10U, 20U, 30U, 255U};
    theme.colors[color_index(os::ui::ColorRole::transparent)] = {0U, 0U, 0U, 0U};
    theme.colors[color_index(os::ui::ColorRole::text_primary)] = {250U, 240U, 230U, 255U};
    return theme;
}

os::ui::FontFaceSet body_faces(const os::ui::ResolvedTextStyle& style) {
    os::ui::FontFaceSet faces{};
    faces.count = style.fallback.count;
    for (std::size_t index = 0U; index < faces.count; ++index) {
        faces.faces[index] = os::ui::FontFaceDescriptor{
            .id = os::ui::FontFaceId{static_cast<std::uint32_t>(index + 1U)},
            .family = style.fallback.families[index],
            .units_per_em = 2048U,
            .weight_min = 300U,
            .weight_max = 700U,
        };
    }
    return faces;
}

os::ui::ShapedText shaped_text(
    const os::ui::SemanticText& text,
    const os::ui::ResolvedTextStyle& style) {
    os::ui::ShapedText shaped{};
    shaped.glyph_count = 2U;
    shaped.run_count = 1U;
    shaped.line_count = 1U;
    shaped.line_height_q6 = style.metrics.line_height_q6;
    shaped.glyphs[0] = os::ui::ShapedGlyph{
        .glyph_id = 1U,
        .cluster_byte_offset = 0U,
        .advance_q6 = os::ui::logical_from_dp(3U),
        .family = os::ui::FontFamilyRole::interface,
    };
    shaped.glyphs[1] = os::ui::ShapedGlyph{
        .glyph_id = 2U,
        .cluster_byte_offset = 1U,
        .advance_q6 = os::ui::logical_from_dp(2U),
        .family = os::ui::FontFamilyRole::interface,
    };
    shaped.runs[0] = os::ui::ShapedRun{
        .first_glyph = 0U,
        .glyph_count = 2U,
        .text_byte_start = 0U,
        .text_byte_length = text.length,
        .family = os::ui::FontFamilyRole::interface,
        .direction = os::ui::TextDirection::left_to_right,
    };
    shaped.lines[0] = os::ui::ShapedLine{.first_glyph = 0U, .glyph_count = 2U};
    return shaped;
}

struct MaskContext final {
    std::array<std::uint8_t, 6U> glyph {{255U, 128U, 255U, 0U, 255U, 255U}};
    bool fail {false};
    bool malformed {false};
};

bool glyph_provider(
    void* context,
    const os::ui::FontFaceDescriptor& face,
    const os::ui::TypographyMetrics& metrics,
    std::uint32_t glyph_id,
    os::ui::RasterScale scale,
    os::ui::GlyphMaskView& output) noexcept {
    auto* state = static_cast<MaskContext*>(context);
    if (state == nullptr || state->fail || face.id.value() == 0U ||
        metrics.size_q6 == 0U || scale.numerator == 0U) {
        return false;
    }
    if (glyph_id == 2U) {
        output = {};
        return true;
    }
    output = os::ui::GlyphMaskView{
        .coverage = state->glyph.data(),
        .byte_count = state->glyph.size(),
        .width = 2U,
        .height = 3U,
        .stride = static_cast<std::uint16_t>(state->malformed ? 1U : 2U),
        .bearing_x_px = 0,
        .bearing_top_px = 2,
    };
    return true;
}

} // namespace

int main() {
    auto style = os::ui::resolve_text_style(os::ui::TypographyRole::body, 100U);
    assert(style);
    auto text = os::ui::make_semantic_text("A ");
    assert(text);
    auto shaped = shaped_text(text.value(), style.value());
    assert(os::ui::shaped_text_valid(text.value(), style.value(), shaped));
    const auto faces = body_faces(style.value());
    auto theme = test_theme();

    constexpr std::uint32_t width = 12U;
    constexpr std::uint32_t height = 10U;
    const os::ui::Rgba8 background{10U, 20U, 30U, 255U};
    const os::ui::Rgba8 half_coverage{130U, 130U, 130U, 255U};
    std::array<os::ui::Rgba8, width * height> pixels{};
    for (auto& pixel : pixels) pixel = background;

    const os::ui::RasterTarget target{
        .pixels = pixels.data(),
        .pixel_count = pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = {1U, os::ui::logical_units_per_dp},
    };
    MaskContext masks{};
    const os::ui::TextRasterOrigin origin{
        .baseline_x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(2U)),
        .first_baseline_y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(5U)),
    };
    auto raster = os::ui::rasterize_shaped_text_masks(
        text.value(),
        style.value(),
        shaped,
        faces,
        os::ui::GlyphMaskProviderBackend{.context = &masks, .resolve = glyph_provider},
        theme,
        os::ui::ColorRole::text_primary,
        origin,
        target);
    assert(raster);
    assert(raster.value().glyphs_seen == 2U);
    assert(raster.value().masks_resolved == 2U);
    assert(raster.value().glyphs_drawn == 1U);
    assert(raster.value().pixel_writes == 5U);

    const auto foreground = theme.colors[color_index(os::ui::ColorRole::text_primary)];
    assert(pixels[3U * width + 2U] == foreground);
    assert(pixels[3U * width + 3U] == half_coverage);
    assert(pixels[4U * width + 2U] == foreground);
    assert(pixels[4U * width + 3U] == background);
    assert(pixels[5U * width + 2U] == foreground);
    assert(pixels[5U * width + 3U] == foreground);

    // A semantic text command must own a real paint rectangle, not merely a
    // shaping width. A glyph mask/bearing that overhangs the node is clipped to
    // that node before target memory is touched. This protects sibling pixels
    // without requiring an offscreen text surface.
    std::array<os::ui::Rgba8, width * height> clipped_pixels{};
    for (auto& pixel : clipped_pixels) pixel = background;
    auto clipped_target = target;
    clipped_target.pixels = clipped_pixels.data();
    clipped_target.pixel_count = clipped_pixels.size();
    const os::ui::LogicalRect clip{
        .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(3U)),
        .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        .width_q6 = os::ui::logical_from_dp(1U),
        .height_q6 = os::ui::logical_from_dp(2U),
    };
    auto clipped = os::ui::rasterize_shaped_text_masks_clipped(
        text.value(),
        style.value(),
        shaped,
        faces,
        os::ui::GlyphMaskProviderBackend{.context = &masks, .resolve = glyph_provider},
        theme,
        os::ui::ColorRole::text_primary,
        origin,
        clip,
        clipped_target);
    assert(clipped);
    assert(clipped.value().glyphs_seen == 2U);
    assert(clipped.value().masks_resolved == 2U);
    assert(clipped.value().glyphs_drawn == 1U);
    assert(clipped.value().pixel_writes == 1U);
    assert(clipped_pixels[5U * width + 3U] == foreground);
    assert(clipped_pixels[3U * width + 3U] == background);
    assert(clipped_pixels[5U * width + 2U] == background);

    auto invalid_clip = clip;
    invalid_clip.width_q6 = 0U;
    auto bad_clip = os::ui::rasterize_shaped_text_masks_clipped(
        text.value(), style.value(), shaped, faces,
        os::ui::GlyphMaskProviderBackend{.context = &masks, .resolve = glyph_provider},
        theme, os::ui::ColorRole::text_primary, origin, invalid_clip, clipped_target);
    assert(!bad_clip);
    expect_ui_error(bad_clip.error(), os::ui::errors::invalid_raster_command);

    auto unavailable = os::ui::rasterize_shaped_text_masks(
        text.value(), style.value(), shaped, faces, {}, theme,
        os::ui::ColorRole::text_primary, {}, target);
    assert(!unavailable);
    expect_ui_error(unavailable.error(), os::ui::errors::glyph_provider_unavailable);

    masks.fail = true;
    auto failed = os::ui::rasterize_shaped_text_masks(
        text.value(), style.value(), shaped, faces,
        os::ui::GlyphMaskProviderBackend{.context = &masks, .resolve = glyph_provider},
        theme, os::ui::ColorRole::text_primary, {}, target);
    assert(!failed);
    expect_ui_error(failed.error(), os::ui::errors::glyph_provider_failed);
    masks.fail = false;

    masks.malformed = true;
    auto malformed = os::ui::rasterize_shaped_text_masks(
        text.value(), style.value(), shaped, faces,
        os::ui::GlyphMaskProviderBackend{.context = &masks, .resolve = glyph_provider},
        theme, os::ui::ColorRole::text_primary, {}, target);
    assert(!malformed);
    expect_ui_error(malformed.error(), os::ui::errors::invalid_glyph_mask);
    masks.malformed = false;

    auto bad_faces = faces;
    bad_faces.faces[0].id = {};
    auto invalid_faces = os::ui::rasterize_shaped_text_masks(
        text.value(), style.value(), shaped, bad_faces,
        os::ui::GlyphMaskProviderBackend{.context = &masks, .resolve = glyph_provider},
        theme, os::ui::ColorRole::text_primary, {}, target);
    assert(!invalid_faces);
    expect_ui_error(invalid_faces.error(), os::ui::errors::invalid_font_face);

    return 0;
}
