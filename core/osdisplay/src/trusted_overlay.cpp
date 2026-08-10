#include <os/display/trusted_overlay.hpp>

#include <cstddef>

namespace os::display {
namespace {

[[nodiscard]] constexpr bool trusted_role_matches(
    SurfaceRole role,
    TrustedPresentation presentation) noexcept {
    switch (presentation) {
    case TrustedPresentation::none:
        return role == SurfaceRole::application || role == SurfaceRole::popup;
    case TrustedPresentation::system_chrome:
        return role == SurfaceRole::system_chrome;
    case TrustedPresentation::secure_system:
        return role == SurfaceRole::secure_system;
    }
    return false;
}

} // namespace

TrustedOverlaySnapshot build_trusted_overlay_snapshot(
    const SceneSnapshot& scene) noexcept {
    TrustedOverlaySnapshot overlay {};
    const std::size_t limit = scene.count < scene.entries.size()
        ? scene.count : scene.entries.size();
    for (std::size_t index = 0U; index < limit; ++index) {
        const SceneEntry& entry = scene.entries[index];
        if (entry.trusted_presentation == TrustedPresentation::none ||
            !trusted_role_matches(entry.surface.role, entry.trusted_presentation) ||
            entry.surface.visibility != SurfaceVisibility::visible ||
            !entry.has_frame || !entry.surface.bounds.nonempty()) {
            continue;
        }
        if (overlay.count >= overlay.entries.size()) break;
        overlay.entries[overlay.count] = TrustedOverlayEntry{
            .surface = entry.surface.id,
            .presentation = entry.trusted_presentation,
            .bounds = entry.surface.bounds,
            .frame_sequence = entry.frame_sequence,
        };
        ++overlay.count;
    }
    return overlay;
}

} // namespace os::display
