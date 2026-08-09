#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/types.hpp>

namespace os::ui {

// Design-system color roles are semantic. Concrete RGB values belong to the
// selected platform theme and are deliberately not application ABI.
enum class ColorRole : std::uint8_t {
    transparent = 0U,
    surface = 1U,
    surface_elevated = 2U,
    text_primary = 3U,
    text_secondary = 4U,
    accent = 5U,
    on_accent = 6U,
    outline = 7U,
    focus = 8U,
    critical = 9U,
    on_critical = 10U,
};

enum class TypographyRole : std::uint8_t {
    body = 1U,
    label = 2U,
    title = 3U,
    headline = 4U,
};

enum class SpacingRole : std::uint8_t {
    none = 0U,
    xxs = 1U,
    xs = 2U,
    sm = 3U,
    md = 4U,
    lg = 5U,
    xl = 6U,
};

enum class ShapeRole : std::uint8_t {
    square = 0U,
    small = 1U,
    medium = 2U,
    large = 3U,
    pill = 4U,
};

struct TypographyMetrics final {
    std::uint32_t size_q6 {0U};
    std::uint32_t line_height_q6 {0U};
    std::uint16_t weight {400U};
};

struct UiStyleToken final {
    StyleTokenId id {};
    ColorRole foreground {ColorRole::text_primary};
    ColorRole background {ColorRole::transparent};
    ColorRole outline {ColorRole::transparent};
    TypographyRole typography {TypographyRole::body};
    SpacingRole horizontal_padding {SpacingRole::none};
    SpacingRole vertical_padding {SpacingRole::none};
    ShapeRole shape {ShapeRole::square};
};

inline constexpr std::uint16_t min_text_scale_percent = 100U;
inline constexpr std::uint16_t max_text_scale_percent = 300U;
inline constexpr std::uint32_t minimum_touch_target_q6 = logical_from_dp(48U);

namespace style_tokens {
inline constexpr StyleTokenId surface {1U};
inline constexpr StyleTokenId body_text {2U};
inline constexpr StyleTokenId secondary_text {3U};
inline constexpr StyleTokenId title_text {4U};
inline constexpr StyleTokenId primary_action {5U};
inline constexpr StyleTokenId secondary_action {6U};
inline constexpr StyleTokenId text_field {7U};
inline constexpr StyleTokenId list_item {8U};
inline constexpr StyleTokenId critical_action {9U};
inline constexpr StyleTokenId focus_ring {10U};
}

[[nodiscard]] bool style_token_valid(StyleTokenId id) noexcept;
[[nodiscard]] os::core::Result<UiStyleToken> resolve_style_token(StyleTokenId id) noexcept;
[[nodiscard]] os::core::Result<TypographyMetrics> typography_metrics(
    TypographyRole role,
    std::uint16_t scale_percent = 100U) noexcept;
[[nodiscard]] os::core::Result<std::uint32_t> spacing_q6(SpacingRole role) noexcept;
[[nodiscard]] os::core::Result<std::uint32_t> shape_radius_q6(ShapeRole role) noexcept;

} // namespace os::ui
