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

[[nodiscard]] os::core::Result<FontFallbackChain> font_fallback_chain(
    TypographyRole role) noexcept;

[[nodiscard]] os::core::Result<ResolvedTextStyle> resolve_text_style(
    TypographyRole role,
    std::uint16_t scale_percent = 100U) noexcept;

} // namespace os::ui
