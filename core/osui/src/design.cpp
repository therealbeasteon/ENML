#include <os/ui/design.hpp>

#include <array>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

inline constexpr std::array<UiStyleToken, 10U> style_table {{
    UiStyleToken{
        .id = style_tokens::surface,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::none,
        .vertical_padding = SpacingRole::none,
        .shape = ShapeRole::square,
    },
    UiStyleToken{
        .id = style_tokens::body_text,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::transparent,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::none,
        .vertical_padding = SpacingRole::none,
        .shape = ShapeRole::square,
    },
    UiStyleToken{
        .id = style_tokens::secondary_text,
        .foreground = ColorRole::text_secondary,
        .background = ColorRole::transparent,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::none,
        .vertical_padding = SpacingRole::none,
        .shape = ShapeRole::square,
    },
    UiStyleToken{
        .id = style_tokens::title_text,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::transparent,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::title,
        .horizontal_padding = SpacingRole::none,
        .vertical_padding = SpacingRole::none,
        .shape = ShapeRole::square,
    },
    UiStyleToken{
        .id = style_tokens::primary_action,
        .foreground = ColorRole::on_accent,
        .background = ColorRole::accent,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::label,
        .horizontal_padding = SpacingRole::md,
        .vertical_padding = SpacingRole::sm,
        .shape = ShapeRole::medium,
    },
    UiStyleToken{
        .id = style_tokens::secondary_action,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface_elevated,
        .outline = ColorRole::outline,
        .typography = TypographyRole::label,
        .horizontal_padding = SpacingRole::md,
        .vertical_padding = SpacingRole::sm,
        .shape = ShapeRole::medium,
    },
    UiStyleToken{
        .id = style_tokens::text_field,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface_elevated,
        .outline = ColorRole::outline,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::md,
        .vertical_padding = SpacingRole::sm,
        .shape = ShapeRole::small,
    },
    UiStyleToken{
        .id = style_tokens::list_item,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::md,
        .vertical_padding = SpacingRole::sm,
        .shape = ShapeRole::square,
    },
    UiStyleToken{
        .id = style_tokens::critical_action,
        .foreground = ColorRole::on_critical,
        .background = ColorRole::critical,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::label,
        .horizontal_padding = SpacingRole::md,
        .vertical_padding = SpacingRole::sm,
        .shape = ShapeRole::medium,
    },
    UiStyleToken{
        .id = style_tokens::focus_ring,
        .foreground = ColorRole::focus,
        .background = ColorRole::transparent,
        .outline = ColorRole::focus,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::none,
        .vertical_padding = SpacingRole::none,
        .shape = ShapeRole::medium,
    },
}};

[[nodiscard]] constexpr bool typography_role_valid(TypographyRole role) noexcept {
    switch (role) {
    case TypographyRole::body:
    case TypographyRole::label:
    case TypographyRole::title:
    case TypographyRole::headline:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr TypographyMetrics base_typography(TypographyRole role) noexcept {
    switch (role) {
    case TypographyRole::body:
        return {logical_from_dp(16U), logical_from_dp(24U), 400U};
    case TypographyRole::label:
        return {logical_from_dp(14U), logical_from_dp(20U), 600U};
    case TypographyRole::title:
        return {logical_from_dp(20U), logical_from_dp(28U), 600U};
    case TypographyRole::headline:
        return {logical_from_dp(28U), logical_from_dp(36U), 600U};
    }
    return {};
}

} // namespace

bool style_token_valid(StyleTokenId id) noexcept {
    if (id.value() == 0U) return true;
    for (const auto& token : style_table) {
        if (token.id == id) return true;
    }
    return false;
}

os::core::Result<UiStyleToken> resolve_style_token(StyleTokenId id) noexcept {
    if (id.value() == 0U) return ui_error(errors::invalid_style);
    for (const auto& token : style_table) {
        if (token.id == id) return token;
    }
    return ui_error(errors::invalid_style);
}

os::core::Result<TypographyMetrics> typography_metrics(
    TypographyRole role,
    std::uint16_t scale_percent) noexcept {
    if (!typography_role_valid(role) || scale_percent < min_text_scale_percent ||
        scale_percent > max_text_scale_percent) {
        return ui_error(errors::invalid_text_scale);
    }

    const TypographyMetrics base = base_typography(role);
    const auto scaled_size = static_cast<std::uint64_t>(base.size_q6) * scale_percent / 100U;
    const auto scaled_line = static_cast<std::uint64_t>(base.line_height_q6) * scale_percent / 100U;
    if (scaled_size == 0U || scaled_line == 0U ||
        scaled_size > max_logical_dimension_q6 || scaled_line > max_logical_dimension_q6) {
        return ui_error(errors::invalid_text_scale);
    }
    return TypographyMetrics{
        .size_q6 = static_cast<std::uint32_t>(scaled_size),
        .line_height_q6 = static_cast<std::uint32_t>(scaled_line),
        .weight = base.weight,
    };
}

os::core::Result<std::uint32_t> spacing_q6(SpacingRole role) noexcept {
    switch (role) {
    case SpacingRole::none: return 0U;
    case SpacingRole::xxs: return logical_from_dp(4U);
    case SpacingRole::xs: return logical_from_dp(8U);
    case SpacingRole::sm: return logical_from_dp(12U);
    case SpacingRole::md: return logical_from_dp(16U);
    case SpacingRole::lg: return logical_from_dp(24U);
    case SpacingRole::xl: return logical_from_dp(32U);
    }
    return ui_error(errors::invalid_style);
}

os::core::Result<std::uint32_t> shape_radius_q6(ShapeRole role) noexcept {
    switch (role) {
    case ShapeRole::square: return 0U;
    case ShapeRole::small: return logical_from_dp(4U);
    case ShapeRole::medium: return logical_from_dp(8U);
    case ShapeRole::large: return logical_from_dp(16U);
    case ShapeRole::pill: return logical_from_dp(999U);
    }
    return ui_error(errors::invalid_style);
}

} // namespace os::ui
