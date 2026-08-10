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

// Concrete font assets stay renderer-private. The platform font provider maps
// semantic family roles to opaque face IDs plus bounded metrics; no path,
// filename, vendor family string or native library handle crosses this seam.
struct FontFaceIdTag;
using FontFaceId = os::core::StrongId<FontFaceIdTag, std::uint32_t>;

struct FontFaceDescriptor final {
    FontFaceId id {};
    FontFamilyRole family {FontFamilyRole::interface};
    std::uint16_t units_per_em {0U};
    std::uint16_t weight_min {0U};
    std::uint16_t weight_max {0U};
};

struct FontFaceSet final {
    std::array<FontFaceDescriptor, max_font_fallback_families> faces {};
    std::size_t count {0U};

    [[nodiscard]] const FontFaceDescriptor* find(FontFamilyRole family) const noexcept;
};

using ResolveFontFaceBackendFn = bool (*)(
    void* context,
    FontFamilyRole family,
    FontFaceDescriptor& output) noexcept;

struct FontProviderBackend final {
    void* context {nullptr};
    ResolveFontFaceBackendFn resolve {nullptr};
};

// These renderer-private bounds intentionally sit above any concrete shaping
// library. The Linux adapter currently uses private production-oriented text
// libraries behind this contract, but their native types/APIs do not become
// ENML ABI and a future backend may replace them without changing app semantics.
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

// Minimal renderer-owned shaping seam retained for isolated backend testing.
// Production integration should prefer FontAwareTextShaperBackend so the
// shaper receives only provider-resolved opaque faces rather than inventing or
// rediscovering font assets outside the validated provider boundary.
using ShapeTextBackendFn = bool (*)(
    void* context,
    const SemanticText& source,
    const ResolvedTextStyle& style,
    ShapedText& output) noexcept;

struct TextShaperBackend final {
    void* context {nullptr};
    ShapeTextBackendFn shape {nullptr};
};

using ShapeTextWithFontsBackendFn = bool (*)(
    void* context,
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const FontFaceSet& faces,
    ShapedText& output) noexcept;

struct FontAwareTextShaperBackend final {
    void* context {nullptr};
    ShapeTextWithFontsBackendFn shape {nullptr};
};

[[nodiscard]] os::core::Result<FontFallbackChain> font_fallback_chain(
    TypographyRole role) noexcept;

[[nodiscard]] os::core::Result<ResolvedTextStyle> resolve_text_style(
    TypographyRole role,
    std::uint16_t scale_percent = 100U) noexcept;

// Resolves exactly the semantic families in a fallback chain through a
// renderer-owned provider. A provider must return a bounded, nonzero opaque
// face ID and sane font metrics for every requested role. It may map multiple
// semantic roles to coordinated assets while keeping implementation details
// private to the renderer.
[[nodiscard]] os::core::Result<FontFaceSet> resolve_font_faces(
    const FontFallbackChain& fallback,
    FontProviderBackend provider) noexcept;

// Validates bounded output from any renderer-owned shaping backend against the
// original UTF-8 text and semantic fallback policy. Concrete platform adapters
// remain subordinate to this ENML contract rather than defining it.
[[nodiscard]] bool shaped_text_valid(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped) noexcept;

// Invokes the minimal renderer-owned backend and validates its output. This is
// useful for isolated shaping-contract tests; real platform integration should
// use shape_text_with_fonts() below.
[[nodiscard]] os::core::Result<ShapedText> shape_text(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    TextShaperBackend backend) noexcept;

// Production-oriented shaping handoff: the font provider resolves semantic
// fallback roles first, then the shaper receives that validated opaque face
// set. No application-controlled font path or native handle can bypass the
// platform font policy through this path.
[[nodiscard]] os::core::Result<ShapedText> shape_text_with_fonts(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const FontFaceSet& faces,
    FontAwareTextShaperBackend backend) noexcept;

// Measurement is derived from validated shaped advances, never from byte or
// code-point counts. This keeps large-text reflow honest with concrete platform
// shaping while preserving ENML's bounded layout contract.
[[nodiscard]] os::core::Result<TextMeasurement> measure_shaped_text(
    const SemanticText& source,
    const ResolvedTextStyle& style,
    const ShapedText& shaped) noexcept;

} // namespace os::ui
