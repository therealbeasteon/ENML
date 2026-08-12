#pragma once

#include <cstdint>

#include <os/ui/identity.hpp>

namespace os::ui {

enum class PressureLevel : std::uint8_t {
    nominal = 0U,
    warm = 1U,
    constrained = 2U,
    critical = 3U,
};

struct RenderPressure final {
    PressureLevel thermal {PressureLevel::nominal};
    PressureLevel gpu {PressureLevel::nominal};
    PressureLevel memory {PressureLevel::nominal};
    bool battery_saver {false};
    bool reduce_motion {false};
    bool reduce_transparency {false};
};

[[nodiscard]] constexpr PressureLevel max_pressure(PressureLevel a, PressureLevel b) noexcept {
    return static_cast<std::uint8_t>(a) >= static_cast<std::uint8_t>(b) ? a : b;
}

[[nodiscard]] constexpr QualityTier maximum_quality(const RenderPressure& pressure) noexcept {
    const auto pressure_level = max_pressure(max_pressure(pressure.thermal, pressure.gpu), pressure.memory);
    if (pressure_level == PressureLevel::critical) return QualityTier::essential;
    if (pressure_level == PressureLevel::constrained || pressure.battery_saver) {
        return QualityTier::continuity;
    }
    if (pressure.reduce_transparency) return QualityTier::continuity;
    if (pressure_level == PressureLevel::warm) return QualityTier::material;
    return QualityTier::ambient;
}

[[nodiscard]] constexpr bool quality_allowed(QualityTier requested, const RenderPressure& pressure) noexcept {
    return static_cast<std::uint8_t>(requested) <= static_cast<std::uint8_t>(maximum_quality(pressure));
}

[[nodiscard]] constexpr bool preserve_spatial_motion(const RenderPressure& pressure) noexcept {
    if (pressure.reduce_motion) return false;
    return maximum_quality(pressure) >= QualityTier::continuity;
}

} // namespace os::ui
