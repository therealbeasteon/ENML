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

[[nodiscard]] constexpr TrustedPresentation trusted_presentation_for_input(
    SurfaceRole role) noexcept {
    switch (role) {
    case SurfaceRole::application:
    case SurfaceRole::popup:
        return TrustedPresentation::none;
    case SurfaceRole::system_chrome:
        return TrustedPresentation::system_chrome;
    case SurfaceRole::secure_system:
        return TrustedPresentation::secure_system;
    }
    return TrustedPresentation::none;
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

os::core::Result<void> Compositor::validate_input_hit(
    const SurfaceInputHit& hit) const noexcept {
    if (!valid_) return display_error(errors::invalid_configuration);
    if (!hit.valid()) return display_error(errors::stale_input_hit);

    const Slot* slot = find_slot(hit.surface);
    if (slot == nullptr || slot->descriptor.owner != hit.owner ||
        slot->descriptor.role != hit.role ||
        slot->descriptor.visibility != SurfaceVisibility::visible ||
        !slot->descriptor.accepts_input || !slot->has_frame ||
        slot->frame_sequence != hit.frame_sequence ||
        slot->descriptor.bounds.width != hit.surface_size.width ||
        slot->descriptor.bounds.height != hit.surface_size.height ||
        trusted_presentation_for_input(slot->descriptor.role) != hit.trusted_presentation) {
        return display_error(errors::stale_input_hit);
    }

    // local_x/local_y were captured relative to the hit surface. Size equality
    // above plus SurfaceInputHit::valid() proves they remain inside the same
    // presented surface-local coordinate space. set_bounds() clears has_frame,
    // so a move/resize cannot preserve an old input hit accidentally.
    return {};
}

} // namespace os::display
