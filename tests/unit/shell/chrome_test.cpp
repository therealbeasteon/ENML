#include <cassert>
#include <cstdint>

#include <os/core/platform_principals.hpp>
#include <os/shell/chrome.hpp>
#include <os/shell/error.hpp>

namespace {

constexpr os::core::PeerIdentity shell{
    .principal = os::core::shell_service_principal,
    .user = os::core::UserId{0U},
    .process = os::core::ProcessId{301U},
};
constexpr os::core::PeerIdentity replacement_shell{
    .principal = os::core::shell_service_principal,
    .user = os::core::UserId{0U},
    .process = os::core::ProcessId{302U},
};
constexpr os::core::PeerIdentity app{
    .principal = {0x4150504348524F4DULL, 1U},
    .user = os::core::UserId{7U},
    .process = os::core::ProcessId{401U},
};

os::display::SurfaceDescriptor chrome_descriptor(
    std::uint32_t generation,
    os::core::PeerIdentity owner = shell) {
    return os::display::SurfaceDescriptor{
        .id = os::display::SurfaceId{
            os::display::make_display_object_value(generation, 4U)},
        .owner = owner,
        .role = os::display::SurfaceRole::system_chrome,
        .bounds = {0, 0, 1080U, 120U},
        .visibility = os::display::SurfaceVisibility::visible,
        .accepts_input = true,
    };
}

os::display::SceneSnapshot scene_for(const os::display::SurfaceDescriptor& descriptor) {
    os::display::SceneSnapshot scene{};
    scene.count = 1U;
    scene.entries[0] = os::display::SceneEntry{
        .surface = descriptor,
        .trusted_presentation = os::display::TrustedPresentation::system_chrome,
    };
    return scene;
}

void expect_shell_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::shell);
    assert(error.code == code);
}

} // namespace

int main() {
    const auto descriptor = chrome_descriptor(12U);
    auto lease = os::shell::accept_system_chrome(shell, descriptor);
    assert(lease);
    assert(lease.value().shell == shell);
    assert(lease.value().surface == descriptor.id);
    assert(lease.value().bounds == descriptor.bounds);

    const auto scene = scene_for(descriptor);
    assert(os::shell::validate_system_chrome(lease.value(), scene));

    // A normal application descriptor cannot be relabelled into a shell chrome
    // lease by the shell model, even if its geometry is identical.
    auto app_owned = descriptor;
    app_owned.owner = app;
    auto app_lease = os::shell::accept_system_chrome(shell, app_owned);
    assert(!app_lease);
    expect_shell_error(app_lease.error(), os::shell::errors::invalid_chrome_lease);

    auto app_role = descriptor;
    app_role.role = os::display::SurfaceRole::application;
    auto wrong_role = os::shell::accept_system_chrome(shell, app_role);
    assert(!wrong_role);
    expect_shell_error(wrong_role.error(), os::shell::errors::invalid_chrome_lease);

    // Trusted presentation remains compositor-authored. If scene state loses or
    // contradicts the system-chrome classification, the prior lease is stale.
    auto untrusted_scene = scene;
    untrusted_scene.entries[0].trusted_presentation = os::display::TrustedPresentation::none;
    auto lost_trust = os::shell::validate_system_chrome(lease.value(), untrusted_scene);
    assert(!lost_trust);
    expect_shell_error(lost_trust.error(), os::shell::errors::stale_chrome_lease);

    // Surface geometry is part of the lease: a compositor-side bounds mutation
    // requires the shell to observe/re-authorize the new state rather than using
    // an old hit/layout assumption.
    auto moved_scene = scene;
    moved_scene.entries[0].surface.bounds.height = 160U;
    auto moved = os::shell::validate_system_chrome(lease.value(), moved_scene);
    assert(!moved);
    expect_shell_error(moved.error(), os::shell::errors::stale_chrome_lease);

    // Compositor restart recovery requires a strictly newer generation and the
    // same exact live shell process identity.
    auto replacement = chrome_descriptor(13U);
    auto recovered = os::shell::replace_system_chrome_after_restart(
        lease.value(), replacement);
    assert(recovered);
    assert(recovered.value().surface == replacement.id);

    auto same_generation = chrome_descriptor(12U);
    auto stale_replacement = os::shell::replace_system_chrome_after_restart(
        lease.value(), same_generation);
    assert(!stale_replacement);
    expect_shell_error(stale_replacement.error(), os::shell::errors::stale_chrome_lease);

    auto other_process = chrome_descriptor(13U, replacement_shell);
    auto inherited = os::shell::replace_system_chrome_after_restart(
        lease.value(), other_process);
    assert(!inherited);
    expect_shell_error(inherited.error(), os::shell::errors::chrome_authority_denied);

    return 0;
}
