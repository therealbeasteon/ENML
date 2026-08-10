#include <os/ui/platform/linux_text_backend.hpp>
#include <os/ui/frame_raster.hpp>

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
    theme.colors[color_index(os::ui::ColorRole::surface)] = {16U, 18U, 24U, 255U};
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

    // Empty semantic text is a valid no-paint input. The platform adapter must
    // not manufacture a glyph or a line merely to satisfy its native libraries.
    auto empty_text = os::ui::make_semantic_text("");
    assert(empty_text);
    auto empty_shaped = os::ui::shape_text_with_fonts(
        empty_text.value(), style.value(), faces.value(), backend.text_shaper());
    assert(empty_shaped);
    assert(empty_shaped.value().glyph_count == 0U);
    assert(empty_shaped.value().run_count == 0U);
    assert(empty_shaped.value().line_count == 0U);

    // The production paragraph seam uses ICU line-break analysis plus
    // HarfBuzz shaping instead of the fake unit-test paragraph backend.
    auto paragraph = os::ui::make_semantic_text("ENML keeps background work quiet");
    assert(paragraph);
    const os::ui::ParagraphConstraints wrap_constraints{
        .max_width_q6 = os::ui::logical_from_dp(110U),
        .max_lines = 4U,
        .wrap = os::ui::ParagraphWrapMode::word,
        .overflow = os::ui::ParagraphOverflowMode::clip,
        .base_direction = os::ui::ParagraphBaseDirection::auto_detect,
    };
    auto wrapped = os::ui::shape_paragraph_with_fonts(
        paragraph.value(),
        style.value(),
        faces.value(),
        wrap_constraints,
        backend.paragraph_shaper());
    assert(wrapped);
    assert(os::ui::paragraph_layout_valid(
        paragraph.value(), style.value(), wrap_constraints, wrapped.value()));
    assert(wrapped.value().line_count >= 2U);
    assert(wrapped.value().line_count <= wrap_constraints.max_lines);

    // Consecutive hard separators preserve the blank visual line without
    // inventing a fake glyph. The blank line owns vertical layout only; the two
    // real text lines remain the only glyph-bearing lines.
    auto blank_middle = os::ui::make_semantic_text("A\n\nB");
    assert(blank_middle);
    const os::ui::ParagraphConstraints hard_break_constraints{
        .max_width_q6 = os::ui::logical_from_dp(80U),
        .max_lines = 4U,
        .wrap = os::ui::ParagraphWrapMode::word,
        .overflow = os::ui::ParagraphOverflowMode::clip,
        .base_direction = os::ui::ParagraphBaseDirection::auto_detect,
    };
    auto hard_breaks = os::ui::shape_paragraph_with_fonts(
        blank_middle.value(),
        style.value(),
        faces.value(),
        hard_break_constraints,
        backend.paragraph_shaper());
    assert(hard_breaks);
    assert(os::ui::paragraph_layout_valid(
        blank_middle.value(), style.value(), hard_break_constraints, hard_breaks.value()));
    assert(hard_breaks.value().line_count == 3U);
    assert(hard_breaks.value().lines[0].glyph_count != 0U);
    assert(hard_breaks.value().lines[1].glyph_count == 0U);
    assert(hard_breaks.value().lines[2].glyph_count != 0U);

    auto only_break = os::ui::make_semantic_text("\n");
    assert(only_break);
    auto blank_paragraph = os::ui::shape_paragraph_with_fonts(
        only_break.value(),
        style.value(),
        faces.value(),
        hard_break_constraints,
        backend.paragraph_shaper());
    assert(blank_paragraph);
    assert(blank_paragraph.value().line_count == 1U);
    assert(blank_paragraph.value().glyph_count == 0U);
    assert(blank_paragraph.value().run_count == 0U);
    auto blank_measurement = os::ui::measure_shaped_text(
        only_break.value(), style.value(), blank_paragraph.value());
    assert(blank_measurement);
    assert(blank_measurement.value().width_q6 == 0U);
    assert(blank_measurement.value().height_q6 == style.value().metrics.line_height_q6);

    // ICU resolves the visual runs while HarfBuzz shapes each directional run.
    // Hebrew bytes remain cluster offsets into the original SemanticText; the
    // application still cannot submit glyph ids or reorder renderer runs.
    auto mixed = os::ui::make_semantic_text("ENML \xD7\x90\xD7\x91\xD7\x92 42");
    assert(mixed);
    const os::ui::ParagraphConstraints bidi_constraints{
        .max_width_q6 = os::ui::logical_from_dp(200U),
        .max_lines = 1U,
        .wrap = os::ui::ParagraphWrapMode::no_wrap,
        .overflow = os::ui::ParagraphOverflowMode::clip,
        .base_direction = os::ui::ParagraphBaseDirection::auto_detect,
    };
    auto bidi = os::ui::shape_paragraph_with_fonts(
        mixed.value(),
        style.value(),
        faces.value(),
        bidi_constraints,
        backend.paragraph_shaper());
    assert(bidi);
    assert(os::ui::paragraph_layout_valid(
        mixed.value(), style.value(), bidi_constraints, bidi.value()));
    bool saw_rtl = false;
    bool saw_ltr = false;
    for (std::size_t index = 0U; index < bidi.value().run_count; ++index) {
        saw_rtl = saw_rtl ||
            bidi.value().runs[index].direction == os::ui::TextDirection::right_to_left;
        saw_ltr = saw_ltr ||
            bidi.value().runs[index].direction == os::ui::TextDirection::left_to_right;
    }
    assert(saw_rtl);
    assert(saw_ltr);

    const auto command_backend = backend.command_backend();
    assert(command_backend.fonts.resolve != nullptr);
    assert(command_backend.paragraphs.shape != nullptr);
    assert(command_backend.line_metrics.resolve != nullptr);
    assert(command_backend.glyphs.resolve != nullptr);

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
    const auto theme = test_theme();
    auto raster = os::ui::rasterize_shaped_text_masks(
        latin.value(),
        style.value(),
        shaped.value(),
        faces.value(),
        backend.glyph_masks(),
        theme,
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

    // Exercise the same production backend through the real ENML command path:
    // semantic text command -> font faces -> paragraph/bidi shaping -> real
    // vertical metrics -> FreeType masks -> caller-owned framebuffer. This is
    // deliberately a deterministic CPU/economy path, not a font demo bypassing
    // RenderCommandBuffer policy.
    std::array<os::ui::Rgba8, width * height> frame_pixels{};
    const os::ui::RasterTarget frame_target{
        .pixels = frame_pixels.data(),
        .pixel_count = frame_pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = {1U, os::ui::logical_units_per_dp},
    };
    os::ui::RenderCommandBuffer commands{};
    commands.revision = 9U;
    commands.count = 2U;

    auto& root = commands.commands[0];
    root.source = os::ui::UiNodeId{1U};
    root.role = os::ui::UiRole::root;
    root.bounds = {0, 0, os::ui::logical_from_dp(width), os::ui::logical_from_dp(height)};
    root.visual.token.background = os::ui::ColorRole::surface;
    root.visual.token.material_tint = os::ui::ColorRole::transparent;
    root.visual.token.outline = os::ui::ColorRole::transparent;
    root.visual.token.material = os::ui::OpticalMaterialRole::opaque;
    root.visual.token.depth = os::ui::DepthRole::flush;
    root.visual.material.opacity_percent = 100U;
    root.contour.role = os::ui::CurveRole::rectilinear;

    auto& label = commands.commands[1];
    label.source = os::ui::UiNodeId{2U};
    label.parent = root.source;
    label.depth = 1U;
    label.role = os::ui::UiRole::text;
    label.bounds = {
        static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        os::ui::logical_from_dp(120U),
        os::ui::logical_from_dp(32U),
    };
    label.content = os::ui::RenderContentKind::text;
    label.visual.token.foreground = os::ui::ColorRole::text_primary;
    label.visual.token.background = os::ui::ColorRole::transparent;
    label.visual.token.material_tint = os::ui::ColorRole::transparent;
    label.visual.token.outline = os::ui::ColorRole::transparent;
    label.visual.token.material = os::ui::OpticalMaterialRole::none;
    label.visual.token.depth = os::ui::DepthRole::flush;
    label.visual.material.opacity_percent = 0U;
    label.contour.role = os::ui::CurveRole::rectilinear;
    label.typography = style.value().metrics;
    label.font_fallbacks = style.value().fallback;
    label.visual_text = latin.value();

    auto frame = os::ui::rasterize_opaque_frame_with_text(
        commands, command_backend, theme, frame_target);
    assert(frame);
    assert(frame.value().geometry.materials.surfaces_filled == 1U);
    assert(frame.value().text.text_commands_seen == 1U);
    assert(frame.value().text.paragraphs_shaped == 1U);
    assert(frame.value().text.glyphs_seen != 0U);
    assert(frame.value().text.glyphs_drawn != 0U);
    assert(frame.value().text.pixel_writes != 0U);

    // At least one pixel in the command-owned text rectangle must differ from
    // the root material, proving the complete command-to-pixel path ran.
    const auto surface = theme.colors[color_index(os::ui::ColorRole::surface)];
    bool saw_text_pixel = false;
    for (std::uint32_t y = 4U; y < 36U && !saw_text_pixel; ++y) {
        for (std::uint32_t x = 4U; x < 124U; ++x) {
            if (frame_pixels[static_cast<std::size_t>(y) * width + x] != surface) {
                saw_text_pixel = true;
                break;
            }
        }
    }
    assert(saw_text_pixel);

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
    assert(invalid.paragraph_shaper().shape == nullptr);
    assert(invalid.line_metrics().resolve == nullptr);
    assert(invalid.glyph_masks().resolve == nullptr);
    assert(invalid.command_backend().paragraphs.shape == nullptr);

    return 0;
}
