#include <os/ui/design.hpp>

#include <array>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

inline constexpr std::array<UiStyleToken, 13U> style_table {{
    UiStyleToken{
        .id = style_tokens::surface,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface,
        .outline = ColorRole::transparent,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::none,
        .vertical_padding = SpacingRole::none,
        .shape = ShapeRole::square,
        .material = OpticalMaterialRole::opaque,
        .depth = DepthRole::flush,
        .curve = CurveRole::rectilinear,
        .motion = MotionRole::none,
        .material_tint = ColorRole::transparent,
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
        .material = OpticalMaterialRole::none,
        .depth = DepthRole::flush,
        .curve = CurveRole::rectilinear,
        .motion = MotionRole::none,
        .material_tint = ColorRole::transparent,
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
        .material = OpticalMaterialRole::none,
        .depth = DepthRole::flush,
        .curve = CurveRole::rectilinear,
        .motion = MotionRole::none,
        .material_tint = ColorRole::transparent,
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
        .material = OpticalMaterialRole::none,
        .depth = DepthRole::flush,
        .curve = CurveRole::rectilinear,
        .motion = MotionRole::none,
        .material_tint = ColorRole::transparent,
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
        .material = OpticalMaterialRole::opaque,
        .depth = DepthRole::raised,
        .curve = CurveRole::continuous,
        .motion = MotionRole::responsive,
        .material_tint = ColorRole::accent_secondary,
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
        .material = OpticalMaterialRole::translucent,
        .depth = DepthRole::raised,
        .curve = CurveRole::continuous,
        .motion = MotionRole::responsive,
        .material_tint = ColorRole::accent_secondary,
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
        .material = OpticalMaterialRole::smoked,
        .depth = DepthRole::inset,
        .curve = CurveRole::soft,
        .motion = MotionRole::micro,
        .material_tint = ColorRole::accent_tertiary,
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
        .material = OpticalMaterialRole::opaque,
        .depth = DepthRole::flush,
        .curve = CurveRole::rectilinear,
        .motion = MotionRole::micro,
        .material_tint = ColorRole::transparent,
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
        .material = OpticalMaterialRole::opaque,
        .depth = DepthRole::raised,
        .curve = CurveRole::continuous,
        .motion = MotionRole::responsive,
        .material_tint = ColorRole::highlight,
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
        .material = OpticalMaterialRole::none,
        .depth = DepthRole::raised,
        .curve = CurveRole::continuous,
        .motion = MotionRole::micro,
        .material_tint = ColorRole::transparent,
    },
    UiStyleToken{
        .id = style_tokens::translucent_panel,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface_elevated,
        .outline = ColorRole::outline,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::lg,
        .vertical_padding = SpacingRole::lg,
        .shape = ShapeRole::large,
        .material = OpticalMaterialRole::crystal,
        .depth = DepthRole::floating,
        .curve = CurveRole::swept,
        .motion = MotionRole::transition,
        .material_tint = ColorRole::accent_secondary,
    },
    UiStyleToken{
        .id = style_tokens::floating_panel,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface_elevated,
        .outline = ColorRole::outline,
        .typography = TypographyRole::body,
        .horizontal_padding = SpacingRole::lg,
        .vertical_padding = SpacingRole::lg,
        .shape = ShapeRole::large,
        .material = OpticalMaterialRole::smoked,
        .depth = DepthRole::floating,
        .curve = CurveRole::continuous,
        .motion = MotionRole::transition,
        .material_tint = ColorRole::accent_tertiary,
    },
    UiStyleToken{
        .id = style_tokens::hero_surface,
        .foreground = ColorRole::text_primary,
        .background = ColorRole::surface_elevated,
        .outline = ColorRole::highlight,
        .typography = TypographyRole::headline,
        .horizontal_padding = SpacingRole::xl,
        .vertical_padding = SpacingRole::xl,
        .shape = ShapeRole::large,
        .material = OpticalMaterialRole::luminous,
        .depth = DepthRole::hero,
        .curve = CurveRole::swept,
        .motion = MotionRole::reveal,
        .material_tint = ColorRole::accent_secondary,
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

os::core::Result<MaterialMetrics> material_metrics(
    OpticalMaterialRole role,
    bool reduce_transparency) noexcept {
    if (reduce_transparency && role != OpticalMaterialRole::none) {
        return MaterialMetrics{
            .opacity_percent = 100U,
            .backdrop_blur_q6 = 0U,
            .tint_percent = 0U,
            .specular_percent = 0U,
            .live_backdrop_allowed = false,
        };
    }

    switch (role) {
    case OpticalMaterialRole::none:
        return MaterialMetrics{0U, 0U, 0U, 0U, false};
    case OpticalMaterialRole::opaque:
        return MaterialMetrics{100U, 0U, 0U, 8U, false};
    case OpticalMaterialRole::translucent:
        return MaterialMetrics{88U, logical_from_dp(18U), 12U, 8U, true};
    case OpticalMaterialRole::crystal:
        return MaterialMetrics{72U, logical_from_dp(24U), 8U, 22U, true};
    case OpticalMaterialRole::smoked:
        return MaterialMetrics{84U, logical_from_dp(20U), 22U, 12U, true};
    case OpticalMaterialRole::luminous:
        return MaterialMetrics{92U, logical_from_dp(12U), 18U, 28U, true};
    }
    return ui_error(errors::invalid_style);
}

os::core::Result<DepthMetrics> depth_metrics(DepthRole role) noexcept {
    switch (role) {
    case DepthRole::flush:
        return DepthMetrics{0U, 0U, 0U};
    case DepthRole::inset:
        return DepthMetrics{logical_from_dp(1U), logical_from_dp(3U), 10U};
    case DepthRole::raised:
        return DepthMetrics{logical_from_dp(2U), logical_from_dp(8U), 16U};
    case DepthRole::floating:
        return DepthMetrics{logical_from_dp(6U), logical_from_dp(18U), 18U};
    case DepthRole::hero:
        return DepthMetrics{logical_from_dp(12U), logical_from_dp(30U), 20U};
    }
    return ui_error(errors::invalid_style);
}

os::core::Result<CurveMetrics> curve_metrics(CurveRole role) noexcept {
    switch (role) {
    case CurveRole::rectilinear:
        return CurveMetrics{0U, 0U, false};
    case CurveRole::soft:
        return CurveMetrics{logical_from_dp(6U), 35U, false};
    case CurveRole::continuous:
        return CurveMetrics{logical_from_dp(12U), 70U, false};
    case CurveRole::swept:
        return CurveMetrics{logical_from_dp(18U), 85U, true};
    case CurveRole::capsule:
        return CurveMetrics{logical_from_dp(999U), 100U, false};
    }
    return ui_error(errors::invalid_style);
}

os::core::Result<MotionMetrics> motion_metrics(
    MotionRole role,
    bool reduce_motion) noexcept {
    if (reduce_motion && role != MotionRole::none) {
        return MotionMetrics{80U, MotionCurve::ease_out, false};
    }

    switch (role) {
    case MotionRole::none:
        return MotionMetrics{0U, MotionCurve::linear, false};
    case MotionRole::micro:
        return MotionMetrics{120U, MotionCurve::spring_precise, true};
    case MotionRole::responsive:
        return MotionMetrics{180U, MotionCurve::spring_precise, true};
    case MotionRole::transition:
        return MotionMetrics{280U, MotionCurve::ease_in_out, true};
    case MotionRole::reveal:
        return MotionMetrics{420U, MotionCurve::spring_soft, true};
    }
    return ui_error(errors::invalid_style);
}

os::core::Result<ResolvedVisualStyle> resolve_visual_style(
    StyleTokenId id,
    VisualPreferences preferences) noexcept {
    auto token_result = resolve_style_token(id);
    if (!token_result) return token_result.error();

    UiStyleToken token = token_result.value();
    const bool reduce_transparency =
        preferences.reduce_transparency || preferences.high_contrast;
    if (preferences.high_contrast) token.material_tint = ColorRole::transparent;

    auto material = material_metrics(token.material, reduce_transparency);
    if (!material) return material.error();
    auto depth = depth_metrics(token.depth);
    if (!depth) return depth.error();
    auto curve = curve_metrics(token.curve);
    if (!curve) return curve.error();
    auto motion = motion_metrics(token.motion, preferences.reduce_motion);
    if (!motion) return motion.error();

    return ResolvedVisualStyle{
        .token = token,
        .material = material.value(),
        .depth = depth.value(),
        .curve = curve.value(),
        .motion = motion.value(),
    };
}

} // namespace os::ui
