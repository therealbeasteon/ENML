#include <os/shell/chrome.hpp>

#include <cstddef>

#include <os/shell/error.hpp>

namespace os::shell {
namespace {

[[nodiscard]] bool descriptor_is_exact_chrome(
    os::core::PeerIdentity shell,
    const os::display::SurfaceDescriptor& descriptor) noexcept {
    return os::core::valid_peer_identity(shell) && descriptor.valid() &&
        descriptor.owner == shell &&
        descriptor.role == os::display::SurfaceRole::system_chrome &&
        descriptor.parent.value() == 0U;
}

[[nodiscard]] const os::display::SceneEntry* find_exact_scene_entry(
    const os::display::SceneSnapshot& scene,
    os::display::SurfaceId surface) noexcept {
    if (scene.count > scene.entries.size()) return nullptr;
    for (std::size_t index = 0U; index < scene.count; ++index) {
        if (scene.entries[index].surface.id == surface) return &scene.entries[index];
    }
    return nullptr;
}

} // namespace

os::core::Result<ShellChromeLease> accept_system_chrome(
    os::core::PeerIdentity exact_shell,
    const os::display::SurfaceDescriptor& descriptor) noexcept {
    if (!os::core::valid_peer_identity(exact_shell)) {
        return shell_error(errors::chrome_authority_denied);
    }
    if (!descriptor_is_exact_chrome(exact_shell, descriptor)) {
        return shell_error(errors::invalid_chrome_lease);
    }
    return ShellChromeLease{
        .shell = exact_shell,
        .surface = descriptor.id,
        .bounds = descriptor.bounds,
    };
}

os::core::Result<void> validate_system_chrome(
    const ShellChromeLease& lease,
    const os::display::SceneSnapshot& scene) noexcept {
    if (!lease.valid() || scene.count > scene.entries.size()) {
        return shell_error(errors::invalid_chrome_lease);
    }

    const auto* entry = find_exact_scene_entry(scene, lease.surface);
    if (entry == nullptr || !entry->surface.valid() ||
        entry->surface.owner != lease.shell ||
        entry->surface.role != os::display::SurfaceRole::system_chrome ||
        entry->surface.parent.value() != 0U ||
        entry->surface.bounds != lease.bounds ||
        entry->trusted_presentation != os::display::TrustedPresentation::system_chrome) {
        return shell_error(errors::stale_chrome_lease);
    }
    return {};
}

os::core::Result<ShellChromeLease> replace_system_chrome_after_restart(
    const ShellChromeLease& prior,
    const os::display::SurfaceDescriptor& replacement) noexcept {
    if (!prior.valid()) return shell_error(errors::invalid_chrome_lease);
    if (!descriptor_is_exact_chrome(prior.shell, replacement)) {
        return shell_error(errors::chrome_authority_denied);
    }

    const std::uint32_t old_generation =
        os::display::display_object_generation(prior.surface.value());
    const std::uint32_t new_generation =
        os::display::display_object_generation(replacement.id.value());
    if (new_generation <= old_generation) {
        return shell_error(errors::stale_chrome_lease);
    }

    return ShellChromeLease{
        .shell = prior.shell,
        .surface = replacement.id,
        .bounds = replacement.bounds,
    };
}

} // namespace os::shell
