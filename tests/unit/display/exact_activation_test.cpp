#include <cassert>
#include <cstdint>

#include <os/core/platform_principals.hpp>
#include <os/display/compositor.hpp>
#include <os/display/error.hpp>

namespace {

constexpr os::core::PrincipalId app_one_principal{0x4150503100000001ULL, 1U};
constexpr os::core::PrincipalId app_two_principal{0x4150503200000001ULL, 2U};
constexpr os::core::PeerIdentity app_one{
    app_one_principal,
    os::core::UserId{7U},
    os::core::ProcessId{101U},
};
constexpr os::core::PeerIdentity app_two{
    app_two_principal,
    os::core::UserId{7U},
    os::core::ProcessId{201U},
};
constexpr os::core::PeerIdentity shell{
    os::core::shell_service_principal,
    os::core::UserId{0U},
    os::core::ProcessId{301U},
};
constexpr os::core::PeerIdentity secure_ui{
    os::core::secure_ui_service_principal,
    os::core::UserId{0U},
    os::core::ProcessId{401U},
};

os::display::Compositor compositor() {
    return os::display::Compositor{
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .safe_insets = {.top = 80U, .bottom = 100U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        os::display::TrustedUiPrincipals{
            .shell = os::core::shell_service_principal,
            .secure_ui = os::core::secure_ui_service_principal,
        },
        9U,
    };
}

os::display::SurfaceDescriptor create_application(
    os::display::Compositor& display,
    os::core::PeerIdentity owner) {
    auto created = display.create_surface(owner, {
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 1080U, 2400U},
        .accepts_input = true,
    });
    assert(created);
    return created.value();
}

void expect_display_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

} // namespace

int main() {
    auto display = compositor();
    assert(display.valid());

    const auto first = create_application(display, app_one);
    const auto second = create_application(display, app_two);

    // The exact-owner primitive is still shell-only. An application cannot
    // foreground itself even when it knows every semantic field of its task.
    auto self_activate = display.activate_application_exact(app_one, app_one, first.id);
    assert(!self_activate);
    expect_display_error(self_activate.error(), os::display::errors::activation_denied);

    // The shell cannot accidentally redirect a stale lifecycle record to a
    // different live application's root. Role/owner mismatch is deliberately
    // one generic denial rather than a target-enumeration oracle.
    auto owner_mismatch = display.activate_application_exact(shell, app_one, second.id);
    assert(!owner_mismatch);
    expect_display_error(owner_mismatch.error(), os::display::errors::activation_denied);

    assert(display.activate_application_exact(shell, app_one, first.id));
    auto scene = display.scene_snapshot();
    assert(scene.count == 2U);
    assert(scene.entries[0].surface.id == second.id);
    assert(scene.entries[1].surface.id == first.id);

    assert(display.activate_application_exact(shell, app_two, second.id));
    scene = display.scene_snapshot();
    assert(scene.entries[0].surface.id == first.id);
    assert(scene.entries[1].surface.id == second.id);

    // A stale generation-scoped id is not silently mapped to a current surface.
    auto stale = display.activate_application_exact(
        shell,
        app_one,
        os::display::SurfaceId{
            os::display::make_display_object_value(8U, 1U)});
    assert(!stale);
    expect_display_error(stale.error(), os::display::errors::unknown_surface);

    // Secure UI has a distinct principal and cannot exercise shell navigation
    // authority merely because it is otherwise trusted system software.
    auto secure_activate = display.activate_application_exact(secure_ui, app_one, first.id);
    assert(!secure_activate);
    expect_display_error(secure_activate.error(), os::display::errors::activation_denied);

    return 0;
}
