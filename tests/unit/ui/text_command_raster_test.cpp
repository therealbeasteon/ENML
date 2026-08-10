#include <os/ui/frame_raster.hpp>
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
    for (auto& color : theme.colors) color = os::ui::Rgba8{24U, 26U, 34U, 255U};
    theme.colors[color_index(os::ui::ColorRole::transparent)] = {0U, 0U, 0U, 0U};
    theme.colors[color_index(os::ui::ColorRole::surface)] = {18U, 20U, 28U, 255U};
    theme.colors[color_index(os::ui::ColorRole::text_primary)] = {242U, 238U, 232U, 255U};
    theme.colors[color_index(os::ui::ColorRole::highlight)] = {250U, 245U, 235U, 255U};
    return theme;
}

bool font_provider(
    void*,
    os::ui::FontFamilyRole family,
    os::ui::FontFaceDescriptor& output) noexcept {
    output = os::ui::FontFaceDescriptor{
        .id = os::ui::FontFaceId{static_cast<std::uint32_t>(family) + 10U},
        .family = family,
        .units_per_em = 2048U,
        .weight_min = 300U,
        .weight_max = 700U,
    };
    return true;
}

bool paragraph_backend(
    void*,
    const os::ui::SemanticText& source,
    const os::ui::ResolvedTextStyle& style,
    const os::ui::FontFaceSet& faces,
    const os::ui::ParagraphConstraints&,
    os::ui::ShapedText& output) noexcept {
    if (source.length != 2U || faces.find(os::ui::FontFamilyRole::interface) == nullptr) {
        return false;
    }
    output = {};
    output.glyph_count = 2U;
    output.run_count = 1U;
    output.line_count = 1U;
    output.line_height_q6 = style.metrics.line_height_q6;
    output.glyphs[0] = os::ui::ShapedGlyph{
        .glyph_id = 1U,
        .cluster_byte_offset = 0U,
        .advance_q6 = os::ui::logical_from_dp(3U),
        .family = os::ui::FontFamilyRole::interface,
    };
    output.glyphs[1] = os::ui::ShapedGlyph{
        .glyph_id = 2U,
        .cluster_byte_offset = 1U,
        .advance_q6 = os::ui::logical_from_dp(3U),
        .family = os::ui::FontFamilyRole::interface,
    };
    output.runs[0] = os::ui::ShapedRun{
        .first_glyph = 0U,
        .glyph_count = 2U,
        .text_byte_start = 0U,
        .text_byte_length = source.length,
        .family = os::ui::FontFamilyRole::interface,
        .direction = os::ui::TextDirection::left_to_right,
    };
    output.lines[0] = os::ui::ShapedLine{.first_glyph = 0U, .glyph_count = 2U};
    return true;
}

struct LineMetricContext final {
    bool malformed {false};
};

bool line_metrics(
    void* context,
    const os::ui::FontFaceDescriptor& face,
    const os::ui::TypographyMetrics& typography,
    os::ui::FontLineMetrics& output) noexcept {
    const auto* state = static_cast<const LineMetricContext*>(context);
    if (state == nullptr || face.id.value() == 0U || typography.size_q6 == 0U) return false;
    output = os::ui::FontLineMetrics{
        .ascent_q6 = os::ui::logical_from_dp(state->malformed ? 23U : 12U),
        .descent_q6 = os::ui::logical_from_dp(4U),
        .line_gap_q6 = os::ui::logical_from_dp(4U),
    };
    return true;
}

struct GlyphContext final {
    std::array<std::uint8_t, 6U> coverage {{255U, 255U, 255U, 255U, 255U, 255U}};
    std::int16_t bearing_x_px {0};
};

bool glyph_provider(
    void* context,
    const os::ui::FontFaceDescriptor& face,
    const os::ui::TypographyMetrics& typography,
    std::uint32_t glyph_id,
    os::ui::RasterScale,
    os::ui::GlyphMaskView& output) noexcept {
    auto* state = static_cast<GlyphContext*>(context);
    if (state == nullptr || face.id.value() == 0U || typography.size_q6 == 0U ||
        glyph_id == 0U) {
        return false;
    }
    output = os::ui::GlyphMaskView{
        .coverage = state->coverage.data(),
        .byte_count = state->coverage.size(),
        .width = 2U,
        .height = 3U,
        .stride = 2U,
        .bearing_x_px = state->bearing_x_px,
        .bearing_top_px = 2,
    };
    return true;
}

os::ui::RenderCommandBuffer commands() {
    os::ui::RenderCommandBuffer buffer{};
    buffer.revision = 22U;
    buffer.count = 2U;

    auto& root = buffer.commands[0];
    root.source = os::ui::UiNodeId{1U};
    root.role = os::ui::UiRole::root;
    root.bounds = {0, 0, os::ui::logical_from_dp(32U), os::ui::logical_from_dp(32U)};
    root.visual.token.background = os::ui::ColorRole::surface;
    root.visual.token.material_tint = os::ui::ColorRole::transparent;
    root.visual.token.outline = os::ui::ColorRole::transparent;
    root.visual.token.material = os::ui::OpticalMaterialRole::opaque;
    root.visual.token.depth = os::ui::DepthRole::flush;
    root.visual.material.opacity_percent = 100U;
    root.contour.role = os::ui::CurveRole::rectilinear;

    auto style = os::ui::resolve_text_style(os::ui::TypographyRole::body, 100U);
    assert(style);
    auto text = os::ui::make_semantic_text("AB");
    assert(text);

    auto& label = buffer.commands[1];
    label.source = os::ui::UiNodeId{2U};
    label.parent = root.source;
    label.depth = 1U;
    label.role = os::ui::UiRole::text;
    label.bounds = {
        static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        static_cast<std::int32_t>(os::ui::logical_from_dp(2U)),
        os::ui::logical_from_dp(20U),
        os::ui::logical_from_dp(24U),
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
    label.visual_text = text.value();
    return buffer;
}

} // namespace

int main() {
    constexpr std::uint32_t width = 32U;
    constexpr std::uint32_t height = 32U;
    std::array<os::ui::Rgba8, width * height> pixels{};

    auto buffer = commands();
    const auto theme = test_theme();
    const os::ui::RasterTarget target{
        .pixels = pixels.data(),
        .pixel_count = pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = {1U, os::ui::logical_units_per_dp},
    };

    LineMetricContext line_context{};
    GlyphContext glyph_context{};
    const os::ui::TextCommandRasterBackend backend{
        .fonts = os::ui::FontProviderBackend{.resolve = font_provider},
        .paragraphs = os::ui::FontAwareParagraphShaperBackend{.shape = paragraph_backend},
        .line_metrics = os::ui::FontLineMetricsBackend{
            .context = &line_context,
            .resolve = line_metrics,
        },
        .glyphs = os::ui::GlyphMaskProviderBackend{
            .context = &glyph_context,
            .resolve = glyph_provider,
        },
    };

    auto frame = os::ui::rasterize_opaque_frame_with_text(buffer, backend, theme, target);
    assert(frame);
    assert(frame.value().geometry.materials.commands_seen == 2U);
    assert(frame.value().text.commands_seen == 2U);
    assert(frame.value().text.text_commands_seen == 1U);
    assert(frame.value().text.paragraphs_shaped == 1U);
    assert(frame.value().text.glyphs_seen == 2U);
    assert(frame.value().text.glyphs_drawn == 2U);
    assert(frame.value().text.pixel_writes == 12U);

    // Body line height is 24dp. With 12dp ascent + 4dp descent, 4dp of
    // leading is placed above the ink, so the baseline is y=18 and the 2px
    // top bearing starts the mask at y=16.
    const auto foreground = theme.colors[color_index(os::ui::ColorRole::text_primary)];
    const auto surface = theme.colors[color_index(os::ui::ColorRole::surface)];
    assert(pixels[16U * width + 4U] == foreground);
    assert(pixels[18U * width + 5U] == foreground);
    assert(pixels[16U * width + 7U] == foreground);

    // Backend ink bearings are allowed to overhang the advance origin, but a
    // RenderContentKind::text node is not allowed to paint into its sibling
    // region. Move both fake masks 2px left: the first glyph is now entirely
    // left of the node's x=4dp clip and stays on the root surface, while the
    // second glyph remains inside and still paints.
    std::array<os::ui::Rgba8, width * height> clipped_pixels{};
    auto clipped_target = target;
    clipped_target.pixels = clipped_pixels.data();
    clipped_target.pixel_count = clipped_pixels.size();
    glyph_context.bearing_x_px = -2;
    auto clipped_frame = os::ui::rasterize_opaque_frame_with_text(
        buffer, backend, theme, clipped_target);
    assert(clipped_frame);
    assert(clipped_frame.value().text.glyphs_seen == 2U);
    assert(clipped_frame.value().text.glyphs_drawn == 1U);
    assert(clipped_frame.value().text.pixel_writes == 6U);
    assert(clipped_pixels[16U * width + 2U] == surface);
    assert(clipped_pixels[16U * width + 3U] == surface);
    assert(clipped_pixels[16U * width + 5U] == foreground);
    glyph_context.bearing_x_px = 0;

    auto no_metrics = backend;
    no_metrics.line_metrics = {};
    auto unavailable = os::ui::rasterize_render_command_text(
        buffer, no_metrics, theme, target);
    assert(!unavailable);
    expect_ui_error(
        unavailable.error(),
        os::ui::errors::font_line_metrics_unavailable);

    line_context.malformed = true;
    auto malformed = os::ui::rasterize_render_command_text(
        buffer, backend, theme, target);
    assert(!malformed);
    expect_ui_error(
        malformed.error(),
        os::ui::errors::invalid_font_line_metrics);
    line_context.malformed = false;

    auto bad_command = buffer;
    bad_command.commands[1].role = os::ui::UiRole::button;
    auto invalid_command = os::ui::rasterize_render_command_text(
        bad_command, backend, theme, target);
    assert(!invalid_command);
    expect_ui_error(invalid_command.error(), os::ui::errors::invalid_raster_command);

    return 0;
}
