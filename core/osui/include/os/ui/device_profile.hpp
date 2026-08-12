#pragma once

#include <cstdint>

namespace os::ui {

enum class DevicePosture : std::uint8_t {
    slab = 0U,
    folded = 1U,
    half_open = 2U,
    unfolded = 3U,
};

enum class WidthClass : std::uint8_t {
    compact = 0U,
    regular = 1U,
    expanded = 2U,
};

enum class HeightClass : std::uint8_t {
    short_ = 0U,
    regular = 1U,
    tall = 2U,
};

enum class CutoutKind : std::uint8_t {
    none = 0U,
    corner = 1U,
    centered = 2U,
    island = 3U,
    hinge = 4U,
};

struct InsetsQ6 final {
    std::uint32_t top {0U};
    std::uint32_t right {0U};
    std::uint32_t bottom {0U};
    std::uint32_t left {0U};
};

struct DeviceProfile final {
    std::uint32_t width_q6 {0U};
    std::uint32_t height_q6 {0U};
    InsetsQ6 safe_insets {};
    DevicePosture posture {DevicePosture::slab};
    WidthClass width_class {WidthClass::compact};
    HeightClass height_class {HeightClass::regular};
    CutoutKind cutout {CutoutKind::none};
    bool rounded_display {false};
    bool one_handed_preferred {true};
};

[[nodiscard]] constexpr bool device_profile_valid(const DeviceProfile& profile) noexcept {
    if (profile.width_q6 == 0U || profile.height_q6 == 0U) return false;
    if (profile.safe_insets.left + profile.safe_insets.right >= profile.width_q6) return false;
    if (profile.safe_insets.top + profile.safe_insets.bottom >= profile.height_q6) return false;
    if (profile.cutout == CutoutKind::hinge && profile.posture == DevicePosture::slab) return false;
    return true;
}

[[nodiscard]] constexpr bool should_recompose_two_pane(const DeviceProfile& profile) noexcept {
    return profile.width_class == WidthClass::expanded || profile.posture == DevicePosture::unfolded;
}

[[nodiscard]] constexpr bool prefer_bottom_reach_controls(const DeviceProfile& profile) noexcept {
    return profile.one_handed_preferred && profile.height_class == HeightClass::tall &&
           profile.width_class != WidthClass::expanded;
}

[[nodiscard]] constexpr bool avoid_center_seam(const DeviceProfile& profile) noexcept {
    return profile.cutout == CutoutKind::hinge || profile.posture == DevicePosture::half_open ||
           profile.posture == DevicePosture::unfolded;
}

} // namespace os::ui
