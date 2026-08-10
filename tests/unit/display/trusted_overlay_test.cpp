#include <os/display/trusted_overlay.hpp>

#include <cassert>
#include <cstddef>

namespace {

os::display::SceneEntry entry(
    std::uint64_t id,
    os::display::SurfaceRole role,
    os::display::TrustedPresentation presentation,
    os::display::SurfaceVisibility visibility,
    bool has_frame) {
    os::display::SceneEntry value{};
    value.surface.id = os::display::SurfaceId{id};
    value.surface.role = role;
    value.surface.bounds = {10, 20, 100U, 50U};
    value.surface.visibility = visibility;
    value.has_frame = has_frame;
    value.frame_sequence = id;
    value.trusted_presentation = presentation;
    return value;
}

} // namespace

int main() {
    os::display::SceneSnapshot scene{};
    scene.count = 6U;

    // Ordinary application pixels are never promoted into a compositor-owned
    // trust overlay.
    scene.entries[0] = entry(
        1U,
        os::display::SurfaceRole::application,
        os::display::TrustedPresentation::none,
        os::display::SurfaceVisibility::visible,
        true);

    scene.entries[1] = entry(
        2U,
        os::display::SurfaceRole::system_chrome,
        os::display::TrustedPresentation::system_chrome,
        os::display::SurfaceVisibility::visible,
        true);

    scene.entries[2] = entry(
        3U,
        os::display::SurfaceRole::secure_system,
        os::display::TrustedPresentation::secure_system,
        os::display::SurfaceVisibility::visible,
        true);

    // A hidden trusted surface has no current on-screen attribution primitive.
    scene.entries[3] = entry(
        4U,
        os::display::SurfaceRole::secure_system,
        os::display::TrustedPresentation::secure_system,
        os::display::SurfaceVisibility::hidden,
        true);

    // No submitted frame means there are no client pixels to attribute yet.
    scene.entries[4] = entry(
        5U,
        os::display::SurfaceRole::system_chrome,
        os::display::TrustedPresentation::system_chrome,
        os::display::SurfaceVisibility::visible,
        false);

    // Even an inconsistent/forged scene record cannot turn an application role
    // into secure presentation at this second derivation boundary.
    scene.entries[5] = entry(
        6U,
        os::display::SurfaceRole::application,
        os::display::TrustedPresentation::secure_system,
        os::display::SurfaceVisibility::visible,
        true);

    const auto overlay = os::display::build_trusted_overlay_snapshot(scene);
    assert(overlay.count == 2U);
    assert(overlay.entries[0].surface == os::display::SurfaceId{2U});
    assert(
        overlay.entries[0].presentation ==
        os::display::TrustedPresentation::system_chrome);
    assert(overlay.entries[0].frame_sequence == 2U);
    assert(overlay.entries[1].surface == os::display::SurfaceId{3U});
    assert(
        overlay.entries[1].presentation ==
        os::display::TrustedPresentation::secure_system);

    // Malformed over-count input is bounded by the fixed scene capacity.
    auto bounded_scene = scene;
    bounded_scene.count = bounded_scene.entries.size() + 100U;
    const auto bounded = os::display::build_trusted_overlay_snapshot(bounded_scene);
    assert(bounded.count == 2U);

    return 0;
}
