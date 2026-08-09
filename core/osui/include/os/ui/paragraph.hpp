#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/text.hpp>

namespace os::ui {

enum class ParagraphWrapMode : std::uint8_t {
    no_wrap = 0U,
    word = 1U,
    grapheme = 2U,
};

enum class ParagraphOverflowMode : std::uint8_t {
    clip = 0U,
    ellipsis = 1U,
};

enum class ParagraphBaseDirection : std::uint8_t {
    auto_detect = 0U,
    left_to_right = 1U,
    right_to_left = 2U,
};

struct ParagraphConstraints final {
    std::uint32_t max_width_q6 {0U};
    std::uint8_t max_lines {1U};
    ParagraphWrapMode wrap {ParagraphWrapMode::word};
    ParagraphOverflowMode overflow {ParagraphOverflowMode::clip};
    ParagraphBaseDirection base_direction {ParagraphBaseDirection::auto_detect};
};

using ShapeParagraphWithFontsBackendFn = bool (*)(
    void* context,
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const FontFaceSet& faces,
    const ParagraphConstraints& constraints,
    ShapedText& output) noexcept;

struct FontAwareParagraphShaperBackend final {
    void* context {nullptr};
    ShapeParagraphWithFontsBackendFn shape {nullptr};
};

// A paragraph backend owns Unicode bidi resolution, script shaping and line
// breaking. ENML validates only the bounded result and does not implement an
// incomplete home-grown Unicode algorithm in the core UI layer.
[[nodiscard]] bool paragraph_layout_valid(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ParagraphConstraints& constraints,
    const ShapedText& shaped) noexcept;

// Production-oriented paragraph handoff. Provider-resolved opaque font faces
// are passed to the backend together with explicit width/line limits. The
// returned ShapedText must already be in normalized paint order per line and
// satisfy ENML's existing UTF-8/run/glyph validation contract.
[[nodiscard]] os::core::Result<ShapedText> shape_paragraph_with_fonts(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const FontFaceSet& faces,
    const ParagraphConstraints& constraints,
    FontAwareParagraphShaperBackend backend) noexcept;

} // namespace os::ui
