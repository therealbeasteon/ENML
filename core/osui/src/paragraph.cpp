#include <os/ui/paragraph.hpp>

#include <cstddef>
#include <cstdint>

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

[[nodiscard]] bool constraints_valid(const ParagraphConstraints& constraints) noexcept {
    return constraints.max_width_q6 != 0U &&
        constraints.max_width_q6 <= max_logical_dimension_q6 &&
        constraints.max_lines != 0U &&
        constraints.max_lines <= max_shaped_lines &&
        wrap_valid(constraints.wrap) && overflow_valid(constraints.overflow) &&
        base_direction_valid(constraints.base_direction);
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

[[nodiscard]] bool style_valid(const ResolvedTextStyle& style) noexcept {
    if (style.metrics.size_q6 == 0U || style.metrics.size_q6 > max_logical_dimension_q6 ||
        style.metrics.line_height_q6 == 0U ||
        style.metrics.line_height_q6 > max_logical_dimension_q6 ||
        style.metrics.weight < 1U || style.metrics.weight > 1000U ||
        style.fallback.count == 0U ||
        style.fallback.count > style.fallback.families.size()) {
        return false;
    }

    for (std::size_t index = 0U; index < style.fallback.count; ++index) {
        if (!font_family_valid(style.fallback.families[index])) return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (style.fallback.families[earlier] == style.fallback.families[index]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool faces_valid(
    const ResolvedTextStyle& style,
    const FontFaceSet& faces) noexcept {
    if (!style_valid(style) || faces.count != style.fallback.count ||
        faces.count > faces.faces.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < faces.count; ++index) {
        const FontFaceDescriptor& face = faces.faces[index];
        if (face.id.value() == 0U || !font_family_valid(face.family) ||
            face.family != style.fallback.families[index] ||
            face.units_per_em < 16U || face.units_per_em > 16384U ||
            face.weight_min < 1U || face.weight_min > 1000U ||
            face.weight_max < 1U || face.weight_max > 1000U ||
            face.weight_min > face.weight_max) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool layout_fits(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ParagraphConstraints& constraints,
    const ShapedText& shaped) noexcept {
    if (shaped.line_count > constraints.max_lines) return false;
    if (constraints.wrap == ParagraphWrapMode::no_wrap && shaped.line_count > 1U) return false;
    auto measurement = measure_shaped_text(source, style, shaped);
    return measurement && measurement.value().width_q6 <= constraints.max_width_q6;
}

} // namespace

bool paragraph_layout_valid(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ParagraphConstraints& constraints,
    const ShapedText& shaped) noexcept {
    return constraints_valid(constraints) && style_valid(style) &&
        shaped_text_valid(source, style, shaped) &&
        layout_fits(source, style, constraints, shaped);
}

os::core::Result<ShapedText> shape_paragraph_with_fonts(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const FontFaceSet& faces,
    const ParagraphConstraints& constraints,
    FontAwareParagraphShaperBackend backend) noexcept {
    if (!semantic_text_valid(source) || !constraints_valid(constraints) ||
        !style_valid(style)) {
        return ui_error(errors::invalid_paragraph_layout);
    }
    if (!faces_valid(style, faces)) return ui_error(errors::invalid_font_face);
    if (backend.shape == nullptr) return ui_error(errors::paragraph_backend_unavailable);

    ShapedText output {};
    if (!backend.shape(
            backend.context,
            source,
            style,
            faces,
            constraints,
            output)) {
        return ui_error(errors::paragraph_backend_failed);
    }
    if (!shaped_text_valid(source, style, output)) {
        return ui_error(errors::invalid_paragraph_layout);
    }
    if (!layout_fits(source, style, constraints, output)) {
        return ui_error(errors::paragraph_layout_limit);
    }
    return output;
}

} // namespace os::ui
