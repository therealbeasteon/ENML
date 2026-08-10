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
    accent_secondary = 11U,
    accent_tertiary = 12U,
    highlight = 13U,
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

// These are semantic optical-material families, not vendor visual effects.
// The renderer owns their physical implementation and may lower quality while
// preserving hierarchy when the device cannot afford live optical effects.
enum class OpticalMaterialRole : std::uint8_t {
    none = 0U,
    opaque = 1U,
    translucent = 2U,
    crystal = 3U,
    smoked = 4U,
    luminous = 5U,
};

enum class DepthRole : std::uint8_t {
    flush = 0U,
    inset = 1U,
    raised = 2U,
    floating = 3U,
    hero = 4U,
};

// CurveRole is intentionally separate from a simple corner radius. Later
// renderers can realize continuous or swept contours rather than reducing the
// visual language to generic rounded rectangles.
enum class CurveRole : std::uint8_t {
    rectilinear = 0U,
    soft = 1U,
    continuous = 2U,
    swept = 3U,
    capsule = 4U,
};

enum class MotionRole : std::uint8_t {
    none = 0U,
    micro = 1U,
    responsive = 2U,
    transition = 3U,
    reveal = 4U,
};

enum class MotionCurve : std::uint8_t {
    linear = 0U,
    ease_out = 1U,
    ease_in_out = 2U,
    spring_soft = 3U,
    spring_precise = 4U,
};

struct TypographyMetrics final {
    std::uint32_t size_q6 {0U};
    std::uint32_t line_height_q6 {0U};
    std::uint16_t weight {400U};
};

struct MaterialMetrics final {
    std::uint8_t opacity_percent {100U};
    std::uint32_t backdrop_blur_q6 {0U};
    std::uint8_t tint_percent {0U};
    std::uint8_t specular_percent {0U};
    bool live_backdrop_allowed {false};
};

struct DepthMetrics final {
    std::uint32_t offset_q6 {0U};
    std::uint32_t blur_q6 {0U};
    std::uint8_t opacity_percent {0U};
};

struct CurveMetrics final {
    std::uint32_t nominal_radius_q6 {0U};
    std::uint8_t smoothing_percent {0U};
    bool asymmetric_contour_allowed {false};
};

struct MotionMetrics final {
    std::uint16_t duration_ms {0U};
    MotionCurve curve {MotionCurve::linear};
    bool spatial_motion_allowed {false};
};

struct VisualPreferences final {
    bool reduce_transparency {false};
    bool reduce_motion {false};
    bool high_contrast {false};
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
    OpticalMaterialRole material {OpticalMaterialRole::none};
    DepthRole depth {DepthRole::flush};
    CurveRole curve {CurveRole::rectilinear};
    MotionRole motion {MotionRole::none};
    ColorRole material_tint {ColorRole::transparent};
};

struct ResolvedVisualStyle final {
    UiStyleToken token {};
    MaterialMetrics material {};
    DepthMetrics depth {};
    CurveMetrics curve {};
    MotionMetrics motion {};
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
inline constexpr StyleTokenId translucent_panel {11U};
inline constexpr StyleTokenId floating_panel {12U};
inline constexpr StyleTokenId hero_surface {13U};
}

[[nodiscard]] bool style_token_valid(StyleTokenId id) noexcept;
[[nodiscard]] os::core::Result<UiStyleToken> resolve_style_token(StyleTokenId id) noexcept;
[[nodiscard]] os::core::Result<TypographyMetrics> typography_metrics(
    TypographyRole role,
    std::uint16_t scale_percent = 100U) noexcept;
[[nodiscard]] os::core::Result<std::uint32_t> spacing_q6(SpacingRole role) noexcept;
[[nodiscard]] os::core::Result<std::uint32_t> shape_radius_q6(ShapeRole role) noexcept;
[[nodiscard]] os::core::Result<MaterialMetrics> material_metrics(
    OpticalMaterialRole role,
    bool reduce_transparency = false) noexcept;
[[nodiscard]] os::core::Result<DepthMetrics> depth_metrics(DepthRole role) noexcept;
[[nodiscard]] os::core::Result<CurveMetrics> curve_metrics(CurveRole role) noexcept;
[[nodiscard]] os::core::Result<MotionMetrics> motion_metrics(
    MotionRole role,
    bool reduce_motion = false) noexcept;
[[nodiscard]] os::core::Result<ResolvedVisualStyle> resolve_visual_style(
    StyleTokenId id,
    VisualPreferences preferences = {}) noexcept;

} // namespace os::ui
