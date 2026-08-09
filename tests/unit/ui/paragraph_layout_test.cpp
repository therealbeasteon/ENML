#include <os/ui/paragraph.hpp>
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

os::ui::FontFaceSet body_faces(const os::ui::ResolvedTextStyle& style) {
    os::ui::FontFaceSet faces{};
    faces.count = style.fallback.count;
    for (std::size_t index = 0U; index < faces.count; ++index) {
        faces.faces[index] = os::ui::FontFaceDescriptor{
            .id = os::ui::FontFaceId{static_cast<std::uint32_t>(100U + index)},
            .family = style.fallback.families[index],
            .units_per_em = 2048U,
            .weight_min = 300U,
            .weight_max = 700U,
        };
    }
    return faces;
}

bool paragraph_backend(
    void* context,
    const os::ui::SemanticText& source,
    const os::ui::ResolvedTextStyle& style,
    const os::ui::FontFaceSet& faces,
    const os::ui::ParagraphConstraints&,
    os::ui::ShapedText& output) noexcept {
    if (context == nullptr || source.length != 4U ||
        faces.find(os::ui::FontFamilyRole::interface) == nullptr) {
        return false;
    }
    const auto advance_q6 = *static_cast<const std::uint32_t*>(context);
    output = {};
    output.glyph_count = 4U;
    output.run_count = 1U;
    output.line_count = 2U;
    output.line_height_q6 = style.metrics.line_height_q6;
    for (std::size_t index = 0U; index < 4U; ++index) {
        output.glyphs[index] = os::ui::ShapedGlyph{
            .glyph_id = static_cast<std::uint32_t>(index + 1U),
            .cluster_byte_offset = static_cast<std::uint16_t>(index),
            .advance_q6 = advance_q6,
            .family = os::ui::FontFamilyRole::interface,
        };
    }
    output.runs[0] = os::ui::ShapedRun{
        .first_glyph = 0U,
        .glyph_count = 4U,
        .text_byte_start = 0U,
        .text_byte_length = source.length,
        .family = os::ui::FontFamilyRole::interface,
        .direction = os::ui::TextDirection::left_to_right,
    };
    output.lines[0] = os::ui::ShapedLine{.first_glyph = 0U, .glyph_count = 2U};
    output.lines[1] = os::ui::ShapedLine{.first_glyph = 2U, .glyph_count = 2U};
    return true;
}

bool failed_backend(
    void*,
    const os::ui::SemanticText&,
    const os::ui::ResolvedTextStyle&,
    const os::ui::FontFaceSet&,
    const os::ui::ParagraphConstraints&,
    os::ui::ShapedText&) noexcept {
    return false;
}

bool malformed_backend(
    void* context,
    const os::ui::SemanticText& source,
    const os::ui::ResolvedTextStyle& style,
    const os::ui::FontFaceSet& faces,
    const os::ui::ParagraphConstraints& constraints,
    os::ui::ShapedText& output) noexcept {
    if (!paragraph_backend(context, source, style, faces, constraints, output)) return false;
    output.glyphs[0].cluster_byte_offset = source.length;
    return true;
}

} // namespace

int main() {
    auto style = os::ui::resolve_text_style(os::ui::TypographyRole::body, 100U);
    assert(style);
    auto text = os::ui::make_semantic_text("ABCD");
    assert(text);
    const auto faces = body_faces(style.value());
    std::uint32_t advance = os::ui::logical_from_dp(8U);

    const os::ui::ParagraphConstraints constraints{
        .max_width_q6 = os::ui::logical_from_dp(16U),
        .max_lines = 2U,
        .wrap = os::ui::ParagraphWrapMode::word,
        .overflow = os::ui::ParagraphOverflowMode::clip,
        .base_direction = os::ui::ParagraphBaseDirection::auto_detect,
    };
    auto shaped = os::ui::shape_paragraph_with_fonts(
        text.value(),
        style.value(),
        faces,
        constraints,
        os::ui::FontAwareParagraphShaperBackend{
            .context = &advance,
            .shape = paragraph_backend,
        });
    assert(shaped);
    assert(shaped.value().line_count == 2U);
    assert(os::ui::paragraph_layout_valid(
        text.value(), style.value(), constraints, shaped.value()));

    auto measured = os::ui::measure_shaped_text(
        text.value(), style.value(), shaped.value());
    assert(measured);
    assert(measured.value().width_q6 == os::ui::logical_from_dp(16U));
    assert(measured.value().line_count == 2U);

    auto narrow = constraints;
    narrow.max_width_q6 = os::ui::logical_from_dp(8U);
    auto too_wide = os::ui::shape_paragraph_with_fonts(
        text.value(), style.value(), faces, narrow,
        os::ui::FontAwareParagraphShaperBackend{
            .context = &advance,
            .shape = paragraph_backend,
        });
    assert(!too_wide);
    expect_ui_error(too_wide.error(), os::ui::errors::paragraph_layout_limit);

    auto one_line = constraints;
    one_line.wrap = os::ui::ParagraphWrapMode::no_wrap;
    one_line.max_lines = 1U;
    auto wrapped_when_forbidden = os::ui::shape_paragraph_with_fonts(
        text.value(), style.value(), faces, one_line,
        os::ui::FontAwareParagraphShaperBackend{
            .context = &advance,
            .shape = paragraph_backend,
        });
    assert(!wrapped_when_forbidden);
    expect_ui_error(
        wrapped_when_forbidden.error(),
        os::ui::errors::paragraph_layout_limit);

    auto unavailable = os::ui::shape_paragraph_with_fonts(
        text.value(), style.value(), faces, constraints, {});
    assert(!unavailable);
    expect_ui_error(
        unavailable.error(),
        os::ui::errors::paragraph_backend_unavailable);

    auto failed = os::ui::shape_paragraph_with_fonts(
        text.value(), style.value(), faces, constraints,
        os::ui::FontAwareParagraphShaperBackend{.shape = failed_backend});
    assert(!failed);
    expect_ui_error(failed.error(), os::ui::errors::paragraph_backend_failed);

    auto malformed = os::ui::shape_paragraph_with_fonts(
        text.value(), style.value(), faces, constraints,
        os::ui::FontAwareParagraphShaperBackend{
            .context = &advance,
            .shape = malformed_backend,
        });
    assert(!malformed);
    expect_ui_error(malformed.error(), os::ui::errors::invalid_paragraph_layout);

    auto invalid_constraints = constraints;
    invalid_constraints.max_width_q6 = 0U;
    auto invalid = os::ui::shape_paragraph_with_fonts(
        text.value(), style.value(), faces, invalid_constraints,
        os::ui::FontAwareParagraphShaperBackend{
            .context = &advance,
            .shape = paragraph_backend,
        });
    assert(!invalid);
    expect_ui_error(invalid.error(), os::ui::errors::invalid_paragraph_layout);

    auto bad_faces = faces;
    bad_faces.faces[0].id = {};
    auto invalid_faces = os::ui::shape_paragraph_with_fonts(
        text.value(), style.value(), bad_faces, constraints,
        os::ui::FontAwareParagraphShaperBackend{
            .context = &advance,
            .shape = paragraph_backend,
        });
    assert(!invalid_faces);
    expect_ui_error(invalid_faces.error(), os::ui::errors::invalid_font_face);

    return 0;
}
