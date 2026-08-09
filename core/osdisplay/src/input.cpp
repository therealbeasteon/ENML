#include <os/display/compositor.hpp>

#include <cstddef>
#include <cstdint>

#include <os/display/error.hpp>

namespace os::display {
namespace {

[[nodiscard]] constexpr bool point_inside(
    Rect bounds,
    std::int32_t x,
    std::int32_t y) noexcept {
    if (x < bounds.x || y < bounds.y) return false;
    const auto right = static_cast<std::int64_t>(bounds.x) +
        static_cast<std::int64_t>(bounds.width);
    const auto bottom = static_cast<std::int64_t>(bounds.y) +
        static_cast<std::int64_t>(bounds.height);
    return static_cast<std::int64_t>(x) < right &&
        static_cast<std::int64_t>(y) < bottom;
}

} // namespace

os::core::Result<SurfaceInputHit> Compositor::hit_test_input(
    std::int32_t x,
    std::int32_t y) const noexcept {
    if (!valid_) return display_error(errors::invalid_configuration);

    const SceneSnapshot snapshot = scene_snapshot();
    for (std::size_t offset = 0U; offset < snapshot.count; ++offset) {
        const std::size_t index = snapshot.count - 1U - offset;
        const SceneEntry& entry = snapshot.entries[index];
        if (entry.surface.visibility != SurfaceVisibility::visible ||
            !entry.surface.accepts_input || !entry.has_frame) {
            continue;
        }
        if (!point_inside(entry.surface.bounds, x, y)) continue;

        const auto local_x64 = static_cast<std::int64_t>(x) - entry.surface.bounds.x;
        const auto local_y64 = static_cast<std::int64_t>(y) - entry.surface.bounds.y;
        SurfaceInputHit hit{
            .surface = entry.surface.id,
            .owner = entry.surface.owner,
            .role = entry.surface.role,
            .surface_size = {
                .width = entry.surface.bounds.width,
                .height = entry.surface.bounds.height,
            },
            .frame_sequence = entry.frame_sequence,
            .local_x = static_cast<std::int32_t>(local_x64),
            .local_y = static_cast<std::int32_t>(local_y64),
            .trusted_presentation = entry.trusted_presentation,
        };
        if (!hit.valid()) return display_error(errors::invalid_geometry);
        return hit;
    }

    return display_error(errors::unknown_surface);
}

} // namespace os::display
