#include <os/display/trusted_overlay.hpp>

#include <cstddef>

namespace os::display {

TrustedOverlaySnapshot build_trusted_overlay_snapshot(
    const SceneSnapshot& scene) noexcept {
    TrustedOverlaySnapshot overlay {};
    const std::size_t limit = scene.count < scene.entries.size()
        ? scene.count : scene.entries.size();
    for (std::size_t index = 0U; index < limit; ++index) {
        const SceneEntry& entry = scene.entries[index];
        if (entry.trusted_presentation == TrustedPresentation::none ||
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
