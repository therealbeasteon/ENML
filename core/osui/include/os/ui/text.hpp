#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/design.hpp>

namespace os::ui {

// Font families remain semantic platform roles. Applications do not select
// font files, filesystem paths, vendor family names or renderer font handles.
// Distinct roles do not require unrelated typefaces: a theme may map display
// and interface to coordinated cuts of one family to preserve visual cohesion.
enum class FontFamilyRole : std::uint8_t {
    interface = 1U,
    display = 2U,
    international = 3U,
    symbols = 4U,
    monospace = 5U,
};

inline constexpr std::size_t max_font_fallback_families = 4U;

struct FontFallbackChain final {
    std::array<FontFamilyRole, max_font_fallback_families> families {};
    std::size_t count {0U};

    [[nodiscard]] bool contains(FontFamilyRole role) const noexcept;
};

struct ResolvedTextStyle final {
    TypographyMetrics metrics {};
    FontFallbackChain fallback {};
};

// These renderer-private bounds intentionally sit above any concrete shaping
// library. A future HarfBuzz-like or platform-native backend may produce glyph
// IDs internally, but applications never submit glyph IDs or font handles.
// The backend output is validated before the renderer trusts it.
inline constexpr std::size_t max_shaped_glyphs = max_semantic_text_bytes;
inline constexpr std::size_t max_shaped_runs = 32U;
inline constexpr std::size_t max_shaped_lines = 16U;

enum class TextDirection : std::uint8_t {
    left_to_right = 1U,
    right_to_left = 2U,
};

struct ShapedGlyph final {
    std::uint32_t glyph_id {0U};
    std::uint16_t cluster_byte_offset {0U};
    std::uint32_t advance_q6 {0U};
    std::int32_t offset_x_q6 {0};
    std::int32_t offset_y_q6 {0};
    FontFamilyRole family {FontFamilyRole::interface};
};

struct ShapedRun final {
    std::uint16_t first_glyph {0U};
    std::uint16_t glyph_count {0U};
    std::uint16_t text_byte_start {0U};
    std::uint16_t text_byte_length {0U};
    FontFamilyRole family {FontFamilyRole::interface};
    TextDirection direction {TextDirection::left_to_right};
};

struct ShapedLine final {
    std::uint16_t first_glyph {0U};
    std::uint16_t glyph_count {0U};
};

struct ShapedText final {
    std::array<ShapedGlyph, max_shaped_glyphs> glyphs {};
    std::size_t glyph_count {0U};
    std::array<ShapedRun, max_shaped_runs> runs {};
    std::size_t run_count {0U};
    std::array<ShapedLine, max_shaped_lines> lines {};
    std::size_t line_count {0U};
    std::uint32_t line_height_q6 {0U};
};

struct TextMeasurement final {
    std::uint32_t width_q6 {0U};
    std::uint32_t height_q6 {0U};
    std::uint16_t line_count {0U};
};

[[nodiscard]] os::core::Result<FontFallbackChain> font_fallback_chain(
    TypographyRole role) noexcept;

[[nodiscard]] os::core::Result<ResolvedTextStyle> resolve_text_style(
    TypographyRole role,
    std::uint16_t scale_percent = 100U) noexcept;

// Validates bounded output from a renderer-owned shaping backend against the
// original UTF-8 text and semantic fallback policy. This does not pretend that
// ENML already ships a production shaper; it establishes the contract the
// eventual shaping implementation must satisfy.
[[nodiscard]] bool shaped_text_valid(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped) noexcept;

// Measurement is derived from validated shaped advances, never from byte or
// code-point counts. This keeps large-text reflow honest once real platform
// font assets and shaping are connected.
[[nodiscard]] os::core::Result<TextMeasurement> measure_shaped_text(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped) noexcept;

} // namespace os::ui
