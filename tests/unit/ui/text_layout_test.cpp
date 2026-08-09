#include <os/ui/text.hpp>
#include <os/ui/types.hpp>

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

os::ui::ShapedText shape_ascii(
    const os::ui::SemanticText& text,
    const os::ui::ResolvedTextStyle& style,
    std::uint32_t advance_q6) {
    os::ui::ShapedText shaped{};
    shaped.glyph_count = static_cast<std::size_t>(text.length);
    shaped.run_count = shaped.glyph_count == 0U ? 0U : 1U;
    shaped.line_count = shaped.glyph_count == 0U ? 0U : 1U;
    shaped.line_height_q6 = style.metrics.line_height_q6;

    for (std::size_t index = 0U; index < shaped.glyph_count; ++index) {
        shaped.glyphs[index] = os::ui::ShapedGlyph{
            .glyph_id = static_cast<std::uint32_t>(index + 1U),
            .cluster_byte_offset = static_cast<std::uint16_t>(index),
            .advance_q6 = advance_q6,
            .family = os::ui::FontFamilyRole::interface,
        };
    }
    if (shaped.run_count != 0U) {
        shaped.runs[0] = os::ui::ShapedRun{
            .first_glyph = 0U,
            .glyph_count = static_cast<std::uint16_t>(shaped.glyph_count),
            .text_byte_start = 0U,
            .text_byte_length = text.length,
            .family = os::ui::FontFamilyRole::interface,
            .direction = os::ui::TextDirection::left_to_right,
        };
        shaped.lines[0] = os::ui::ShapedLine{
            .first_glyph = 0U,
            .glyph_count = static_cast<std::uint16_t>(shaped.glyph_count),
        };
    }
    return shaped;
}

bool ascii_backend(
    void* context,
    const os::ui::SemanticText& text,
    const os::ui::ResolvedTextStyle& style,
    os::ui::ShapedText& output) noexcept {
    if (context == nullptr) return false;
    const auto advance_q6 = *static_cast<const std::uint32_t*>(context);
    output = shape_ascii(text, style, advance_q6);
    return true;
}

bool failed_backend(
    void*,
    const os::ui::SemanticText&,
    const os::ui::ResolvedTextStyle&,
    os::ui::ShapedText&) noexcept {
    return false;
}

bool malformed_backend(
    void* context,
    const os::ui::SemanticText& text,
    const os::ui::ResolvedTextStyle& style,
    os::ui::ShapedText& output) noexcept {
    if (!ascii_backend(context, text, style, output)) return false;
    if (output.glyph_count != 0U) output.glyphs[0].cluster_byte_offset = text.length;
    return true;
}

} // namespace

int main() {
    auto body_style = os::ui::resolve_text_style(os::ui::TypographyRole::body, 100U);
    assert(body_style);
    assert(body_style.value().fallback.contains(os::ui::FontFamilyRole::interface));
    assert(body_style.value().fallback.contains(os::ui::FontFamilyRole::international));
    assert(!body_style.value().fallback.contains(os::ui::FontFamilyRole::display));

    auto text = os::ui::make_semantic_text("Hello world");
    assert(text);
    std::uint32_t advance = os::ui::logical_from_dp(8U);
    auto shaped = shape_ascii(text.value(), body_style.value(), advance);
    assert(os::ui::shaped_text_valid(text.value(), body_style.value(), shaped));

    auto backend_shaped = os::ui::shape_text(
        text.value(),
        body_style.value(),
        os::ui::TextShaperBackend{
            .context = &advance,
            .shape = ascii_backend,
        });
    assert(backend_shaped);
    assert(backend_shaped.value().glyph_count == shaped.glyph_count);

    auto unavailable = os::ui::shape_text(
        text.value(),
        body_style.value(),
        os::ui::TextShaperBackend{});
    assert(!unavailable);
    expect_ui_error(unavailable.error(), os::ui::errors::text_shaper_unavailable);

    auto failed = os::ui::shape_text(
        text.value(),
        body_style.value(),
        os::ui::TextShaperBackend{.shape = failed_backend});
    assert(!failed);
    expect_ui_error(failed.error(), os::ui::errors::text_shaper_failed);

    auto malformed = os::ui::shape_text(
        text.value(),
        body_style.value(),
        os::ui::TextShaperBackend{
            .context = &advance,
            .shape = malformed_backend,
        });
    assert(!malformed);
    expect_ui_error(malformed.error(), os::ui::errors::invalid_text_shape);

    auto measured = os::ui::measure_shaped_text(text.value(), body_style.value(), shaped);
    assert(measured);
    assert(measured.value().width_q6 == os::ui::logical_from_dp(88U));
    assert(measured.value().height_q6 == body_style.value().metrics.line_height_q6);
    assert(measured.value().line_count == 1U);

    auto wrapped_text = os::ui::make_semantic_text("ABCD");
    assert(wrapped_text);
    auto wrapped = shape_ascii(wrapped_text.value(), body_style.value(), advance);
    wrapped.line_count = 2U;
    wrapped.lines[0] = os::ui::ShapedLine{.first_glyph = 0U, .glyph_count = 2U};
    wrapped.lines[1] = os::ui::ShapedLine{.first_glyph = 2U, .glyph_count = 2U};
    assert(os::ui::shaped_text_valid(wrapped_text.value(), body_style.value(), wrapped));
    auto wrapped_measurement =
        os::ui::measure_shaped_text(wrapped_text.value(), body_style.value(), wrapped);
    assert(wrapped_measurement);
    assert(wrapped_measurement.value().width_q6 == os::ui::logical_from_dp(16U));
    assert(
        wrapped_measurement.value().height_q6 ==
        body_style.value().metrics.line_height_q6 * 2U);
    assert(wrapped_measurement.value().line_count == 2U);

    // The validator permits visual-order clusters inside a right-to-left run;
    // it only requires that cluster offsets are valid UTF-8 boundaries inside
    // that run. A real renderer-owned shaper determines the final glyph order.
    auto rtl_text = os::ui::make_semantic_text("\xD7\x90\xD7\x91");
    assert(rtl_text);
    os::ui::ShapedText rtl{};
    rtl.glyph_count = 2U;
    rtl.run_count = 1U;
    rtl.line_count = 1U;
    rtl.line_height_q6 = body_style.value().metrics.line_height_q6;
    rtl.glyphs[0] = os::ui::ShapedGlyph{
        .glyph_id = 10U,
        .cluster_byte_offset = 2U,
        .advance_q6 = advance,
        .family = os::ui::FontFamilyRole::international,
    };
    rtl.glyphs[1] = os::ui::ShapedGlyph{
        .glyph_id = 11U,
        .cluster_byte_offset = 0U,
        .advance_q6 = advance,
        .family = os::ui::FontFamilyRole::international,
    };
    rtl.runs[0] = os::ui::ShapedRun{
        .first_glyph = 0U,
        .glyph_count = 2U,
        .text_byte_start = 0U,
        .text_byte_length = rtl_text.value().length,
        .family = os::ui::FontFamilyRole::international,
        .direction = os::ui::TextDirection::right_to_left,
    };
    rtl.lines[0] = os::ui::ShapedLine{.first_glyph = 0U, .glyph_count = 2U};
    assert(os::ui::shaped_text_valid(rtl_text.value(), body_style.value(), rtl));

    auto invalid_cluster = rtl;
    invalid_cluster.glyphs[0].cluster_byte_offset = 1U;
    assert(!os::ui::shaped_text_valid(rtl_text.value(), body_style.value(), invalid_cluster));
    auto invalid_cluster_measurement =
        os::ui::measure_shaped_text(rtl_text.value(), body_style.value(), invalid_cluster);
    assert(!invalid_cluster_measurement);
    expect_ui_error(invalid_cluster_measurement.error(), os::ui::errors::invalid_text_shape);

    auto invalid_family = shaped;
    invalid_family.runs[0].family = os::ui::FontFamilyRole::display;
    for (std::size_t index = 0U; index < invalid_family.glyph_count; ++index) {
        invalid_family.glyphs[index].family = os::ui::FontFamilyRole::display;
    }
    assert(!os::ui::shaped_text_valid(text.value(), body_style.value(), invalid_family));

    auto overflow_text = os::ui::make_semantic_text("AA");
    assert(overflow_text);
    auto overflow = shape_ascii(
        overflow_text.value(),
        body_style.value(),
        os::ui::max_logical_dimension_q6);
    assert(os::ui::shaped_text_valid(overflow_text.value(), body_style.value(), overflow));
    auto overflow_measurement =
        os::ui::measure_shaped_text(overflow_text.value(), body_style.value(), overflow);
    assert(!overflow_measurement);
    expect_ui_error(overflow_measurement.error(), os::ui::errors::text_shape_limit);

    auto empty_text = os::ui::make_semantic_text("");
    assert(empty_text);
    auto empty = shape_ascii(empty_text.value(), body_style.value(), advance);
    assert(os::ui::shaped_text_valid(empty_text.value(), body_style.value(), empty));
    auto empty_measurement =
        os::ui::measure_shaped_text(empty_text.value(), body_style.value(), empty);
    assert(empty_measurement);
    assert(empty_measurement.value().width_q6 == 0U);
    assert(empty_measurement.value().height_q6 == 0U);
    assert(empty_measurement.value().line_count == 0U);

    auto wrong_line_height = shaped;
    wrong_line_height.line_height_q6 += 1U;
    assert(!os::ui::shaped_text_valid(text.value(), body_style.value(), wrong_line_height));

    return 0;
}
