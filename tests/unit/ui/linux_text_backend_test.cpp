#include <os/ui/platform/linux_text_backend.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#ifndef EMNL_TEST_FONT_PATH
#error "EMNL_TEST_FONT_PATH must name the CI font asset"
#endif

namespace {

constexpr std::size_t color_index(os::ui::ColorRole role) {
    return static_cast<std::size_t>(role);
}

os::ui::RasterTheme test_theme() {
    os::ui::RasterTheme theme{};
    for (auto& color : theme.colors) color = {0U, 0U, 0U, 255U};
    theme.colors[color_index(os::ui::ColorRole::transparent)] = {0U, 0U, 0U, 0U};
    theme.colors[color_index(os::ui::ColorRole::text_primary)] = {235U, 238U, 244U, 255U};
    return theme;
}

} // namespace

int main() {
    const os::ui::platform::LinuxFontFiles files{
        .interface_path = EMNL_TEST_FONT_PATH,
        .display_path = EMNL_TEST_FONT_PATH,
        .international_path = EMNL_TEST_FONT_PATH,
        .symbols_path = EMNL_TEST_FONT_PATH,
        .monospace_path = EMNL_TEST_FONT_PATH,
    };
    os::ui::platform::LinuxTextBackend backend{files};
    assert(backend.valid());

    auto style = os::ui::resolve_text_style(os::ui::TypographyRole::body, 100U);
    assert(style);
    auto faces = os::ui::resolve_font_faces(style.value().fallback, backend.font_provider());
    assert(faces);
    assert(faces.value().count == style.value().fallback.count);
    for (std::size_t index = 0U; index < faces.value().count; ++index) {
        assert(faces.value().faces[index].id.value() != 0U);
        assert(faces.value().faces[index].units_per_em != 0U);
    }

    auto latin = os::ui::make_semantic_text("office");
    assert(latin);
    auto shaped = os::ui::shape_text_with_fonts(
        latin.value(), style.value(), faces.value(), backend.text_shaper());
    assert(shaped);
    assert(os::ui::shaped_text_valid(latin.value(), style.value(), shaped.value()));
    assert(shaped.value().glyph_count != 0U);
    assert(shaped.value().run_count != 0U);
    assert(shaped.value().line_count == 1U);
    auto measured = os::ui::measure_shaped_text(latin.value(), style.value(), shaped.value());
    assert(measured);
    assert(measured.value().width_q6 != 0U);

    const os::ui::FontFaceDescriptor* face =
        faces.value().find(shaped.value().glyphs[0].family);
    assert(face != nullptr);
    os::ui::FontLineMetrics metrics{};
    const auto metrics_backend = backend.line_metrics();
    assert(metrics_backend.resolve != nullptr);
    assert(metrics_backend.resolve(
        metrics_backend.context, *face, style.value().metrics, metrics));
    assert(metrics.ascent_q6 != 0U);
    assert(
        static_cast<std::uint64_t>(metrics.ascent_q6) + metrics.descent_q6 <=
        style.value().metrics.line_height_q6);

    // This exercises a real FreeType gray coverage bitmap through ENML's
    // existing renderer-private mask seam. The test never exposes the font path
    // or native face through SemanticText/FontFaceDescriptor.
    constexpr std::uint32_t width = 160U;
    constexpr std::uint32_t height = 48U;
    std::array<os::ui::Rgba8, width * height> pixels{};
    const os::ui::RasterTarget target{
        .pixels = pixels.data(),
        .pixel_count = pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = {1U, os::ui::logical_units_per_dp},
    };
    auto raster = os::ui::rasterize_shaped_text_masks(
        latin.value(),
        style.value(),
        shaped.value(),
        faces.value(),
        backend.glyph_masks(),
        test_theme(),
        os::ui::ColorRole::text_primary,
        os::ui::TextRasterOrigin{
            .baseline_x_q6 = os::ui::logical_from_dp(4U),
            .first_baseline_y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(24U)),
        },
        target);
    assert(raster);
    assert(raster.value().glyphs_seen == shaped.value().glyph_count);
    assert(raster.value().masks_resolved == shaped.value().glyph_count);
    assert(raster.value().glyphs_drawn != 0U);
    assert(raster.value().pixel_writes != 0U);

    // Invalid renderer configuration fails closed before any callback is
    // exposed. This is a platform/backend failure, not an application fallback
    // to arbitrary font files.
    const os::ui::platform::LinuxFontFiles missing{
        .interface_path = "/definitely/not/an/enml/font.ttf",
        .display_path = EMNL_TEST_FONT_PATH,
        .international_path = EMNL_TEST_FONT_PATH,
        .symbols_path = EMNL_TEST_FONT_PATH,
        .monospace_path = EMNL_TEST_FONT_PATH,
    };
    os::ui::platform::LinuxTextBackend invalid{missing};
    assert(!invalid.valid());
    assert(invalid.font_provider().resolve == nullptr);
    assert(invalid.text_shaper().shape == nullptr);
    assert(invalid.line_metrics().resolve == nullptr);
    assert(invalid.glyph_masks().resolve == nullptr);

    return 0;
}
