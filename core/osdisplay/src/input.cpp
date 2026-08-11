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

[[nodiscard]] constexpr bool rects_overlap(Rect a, Rect b) noexcept {
    const auto a_right = static_cast<std::int64_t>(a.x) + static_cast<std::int64_t>(a.width);
    const auto a_bottom = static_cast<std::int64_t>(a.y) + static_cast<std::int64_t>(a.height);
    const auto b_right = static_cast<std::int64_t>(b.x) + static_cast<std::int64_t>(b.width);
    const auto b_bottom = static_cast<std::int64_t>(b.y) + static_cast<std::int64_t>(b.height);
    return static_cast<std::int64_t>(a.x) < b_right && static_cast<std::int64_t>(b.x) < a_right &&
        static_cast<std::int64_t>(a.y) < b_bottom && static_cast<std::int64_t>(b.y) < a_bottom;
}

// Whether a surface above the target, belonging to a different principal, was
// covering it when the event was taken.
//
// Only surfaces above the target matter, and only ones drawing pixels the user
// could have been looking at: visible and holding a frame. A surface that
// accepts input is not a concern here because hit testing would have chosen it
// instead - the danger is precisely the surface that draws but declines input,
// since the user aims at its pixels and the event falls through to whatever is
// beneath.
//
// Surfaces owned by the same principal are excluded. A principal composing its
// own interface out of several surfaces is not deceiving itself, and treating
// that as an attack would make ordinary layering impossible.
struct Obscuration final {
    bool at_point {false};
    bool partial {false};
};

[[nodiscard]] Obscuration obscuration_above(
    const SceneSnapshot& snapshot,
    std::size_t target_index,
    os::core::PeerIdentity target_owner,
    Rect target_bounds,
    std::int32_t x,
    std::int32_t y) noexcept {
    Obscuration result{};
    for (std::size_t index = target_index + 1U; index < snapshot.count; ++index) {
        const SceneEntry& above = snapshot.entries[index];
        if (above.surface.visibility != SurfaceVisibility::visible || !above.has_frame) {
            continue;
        }
        if (above.surface.owner == target_owner) {
            continue;
        }
        if (point_inside(above.surface.bounds, x, y)) {
            result.at_point = true;
        }
        if (rects_overlap(above.surface.bounds, target_bounds)) {
            result.partial = true;
        }
    }
    return result;
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

        // Refuse rather than report. A hit that is obscured is one where the
        // pixels the user aimed at and the surface about to receive the event
        // belong to different principals, and no caller downstream can recover
        // the user's actual intent from that. Answering "here is a hit, but it
        // may be a lie" invites the caller to use it anyway; ENML's other
        // boundaries all refuse in this situation and this one does too.
        const auto obscured = obscuration_above(
            snapshot, index, entry.surface.owner, entry.surface.bounds, x, y);
        if (obscured.at_point || obscured.partial) {
            return display_error(errors::obscured_input);
        }

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
            .obscured_at_point = false,
            .partially_obscured = false,
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
    // A hit carrying either flag was never produced by this compositor.
    if (hit.obscured_at_point || hit.partially_obscured) {
        return display_error(errors::obscured_input);
    }

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

    // Re-derive obscuration from the current scene rather than trusting the
    // flags on the hit. Another principal can raise a surface over the target
    // between hit testing and delivery without touching the target at all, so
    // its frame sequence and bounds are unchanged and every check above still
    // passes. Checking only what the hit carries would authorize an event
    // taken before the overlay and delivered after it.
    {
        const SceneSnapshot snapshot = scene_snapshot();
        for (std::size_t index = 0U; index < snapshot.count; ++index) {
            if (snapshot.entries[index].surface.id != hit.surface) continue;
            const auto obscured = obscuration_above(
                snapshot,
                index,
                hit.owner,
                snapshot.entries[index].surface.bounds,
                snapshot.entries[index].surface.bounds.x + hit.local_x,
                snapshot.entries[index].surface.bounds.y + hit.local_y);
            if (obscured.at_point || obscured.partial) {
                return display_error(errors::obscured_input);
            }
            break;
        }
    }

    // local_x/local_y were captured relative to the hit surface. Size equality
    // above plus SurfaceInputHit::valid() proves they remain inside the same
    // presented surface-local coordinate space. set_bounds() clears has_frame,
    // so a move/resize cannot preserve an old input hit accidentally.
    return {};
}

} // namespace os::display
