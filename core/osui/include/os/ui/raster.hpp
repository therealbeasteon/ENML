#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/design.hpp>
#include <os/ui/renderer.hpp>

namespace os::ui {

// Renderer-private packed color. Applications continue to use semantic
// ColorRole values; concrete color assignment remains a platform/theme choice.
struct Rgba8 final {
    std::uint8_t red {0U};
    std::uint8_t green {0U};
    std::uint8_t blue {0U};
    std::uint8_t alpha {255U};

    [[nodiscard]] friend constexpr bool operator==(const Rgba8&, const Rgba8&) = default;
};

inline constexpr std::size_t raster_color_role_count = 14U;

struct RasterTheme final {
    std::array<Rgba8, raster_color_role_count> colors {};
};

// Maps logical Q6 geometry into physical raster pixels. numerator/denominator
// describes pixels per logical Q6 unit; 1/64 is 1 physical pixel per dp,
// 2/64 is 2 physical pixels per dp, and so on.
struct RasterScale final {
    std::uint32_t numerator {1U};
    std::uint32_t denominator {logical_units_per_dp};
};

struct RasterTarget final {
    Rgba8* pixels {nullptr};
    std::size_t pixel_count {0U};
    std::uint32_t width {0U};
    std::uint32_t height {0U};
    std::uint32_t stride {0U};
    RasterScale scale {};
};

struct RasterStats final {
    std::uint16_t commands_seen {0U};
    std::uint16_t surfaces_filled {0U};
    std::uint64_t pixel_writes {0U};
};

inline constexpr std::uint32_t max_raster_dimension = 4096U;

// First concrete ENML paint stage: bounded, deterministic, CPU-side and
// intentionally opaque. It realizes semantic palette roles, material tint,
// focus/outline state and per-corner authored contour asymmetry without yet
// depending on blur, shaders, font glyph masks, GPU APIs or live backdrop
// sampling. Rich optical effects are layered later; identity does not depend
// on those effects being present.
[[nodiscard]] os::core::Result<RasterStats> rasterize_opaque_materials(
    const RenderCommandBuffer& commands,
    const RasterTheme& theme,
    RasterTarget target) noexcept;

} // namespace os::ui
