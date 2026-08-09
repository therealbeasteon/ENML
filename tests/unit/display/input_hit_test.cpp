#include <cassert>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/display/compositor.hpp>
#include <os/display/error.hpp>

namespace {

constexpr os::core::PrincipalId app_principal{0x4150500000000101ULL, 1U};
constexpr os::core::PrincipalId shell_principal{0x5348454C4C000101ULL, 1U};
constexpr os::core::PrincipalId secure_principal{0x5345435552450101ULL, 1U};

constexpr os::core::PeerIdentity app{
    app_principal,
    os::core::UserId{7U},
    os::core::ProcessId{101U},
};
constexpr os::core::PeerIdentity secure{
    secure_principal,
    os::core::UserId{0U},
    os::core::ProcessId{401U},
};

void expect_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

os::display::BufferId buffer_for(os::display::SurfaceId surface) {
    return os::display::BufferId{surface.value() + 1000U};
}

void show_and_frame(
    os::display::Compositor& compositor,
    os::core::PeerIdentity owner,
    const os::display::SurfaceDescriptor& surface,
    std::uint64_t sequence) {
    assert(compositor.set_visibility(
        owner,
        surface.id,
        os::display::SurfaceVisibility::visible));
    os::display::FrameSubmission submission{
        .surface = surface.id,
        .buffer = buffer_for(surface.id),
        .sequence = sequence,
        .buffer_slot = 0U,
        .damage_count = 1U,
    };
    submission.damage[0] = {
        0,
        0,
        surface.bounds.width,
        surface.bounds.height,
    };
    assert(compositor.submit_frame(owner, submission, 1'000'000U));
}

} // namespace

int main() {
    os::display::Compositor compositor{
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .safe_insets = {.top = 80U, .bottom = 100U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        {.shell = shell_principal, .secure_ui = secure_principal},
    };
    assert(compositor.valid());

    auto app_surface = compositor.create_surface(app, {
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 1080U, 2400U},
        .accepts_input = true,
    });
    assert(app_surface);
    auto popup = compositor.create_surface(app, {
        .role = os::display::SurfaceRole::popup,
        .parent = app_surface.value().id,
        .bounds = {100, 100, 300U, 200U},
        .accepts_input = true,
    });
    assert(popup);
    auto secure_surface = compositor.create_surface(secure, {
        .role = os::display::SurfaceRole::secure_system,
        .bounds = {150, 120, 200U, 160U},
        .accepts_input = true,
    });
    assert(secure_surface);

    show_and_frame(compositor, app, app_surface.value(), 3U);
    show_and_frame(compositor, app, popup.value(), 5U);
    show_and_frame(compositor, secure, secure_surface.value(), 7U);

    // Secure-system presentation is globally topmost and the compositor
    // localizes the coordinate before handing it to a future privileged input
    // bridge. The result is tied to the exact frame the user was shown.
    auto secure_hit = compositor.hit_test_input(160, 130);
    assert(secure_hit);
    assert(secure_hit.value().valid());
    assert(secure_hit.value().surface == secure_surface.value().id);
    assert(secure_hit.value().owner == secure);
    assert(secure_hit.value().role == os::display::SurfaceRole::secure_system);
    assert(secure_hit.value().surface_size.width == 200U);
    assert(secure_hit.value().surface_size.height == 160U);
    assert(secure_hit.value().frame_sequence == 7U);
    assert(secure_hit.value().local_x == 10);
    assert(secure_hit.value().local_y == 10);
    assert(
        secure_hit.value().trusted_presentation ==
        os::display::TrustedPresentation::secure_system);
    assert(compositor.validate_input_hit(secure_hit.value()));

    auto legacy_secure = compositor.hit_test(160, 130);
    assert(legacy_secure && legacy_secure.value() == secure_hit.value().surface);

    assert(compositor.set_visibility(
        secure,
        secure_surface.value().id,
        os::display::SurfaceVisibility::hidden));
    auto stale_secure = compositor.validate_input_hit(secure_hit.value());
    assert(!stale_secure);
    expect_error(stale_secure.error(), os::display::errors::stale_input_hit);

    // With secure UI hidden, the application popup owns the same point. The
    // event recipient learns its own local point, not global scene geometry.
    auto popup_hit = compositor.hit_test_input(160, 130);
    assert(popup_hit);
    assert(popup_hit.value().surface == popup.value().id);
    assert(popup_hit.value().owner == app);
    assert(popup_hit.value().role == os::display::SurfaceRole::popup);
    assert(popup_hit.value().frame_sequence == 5U);
    assert(popup_hit.value().local_x == 60);
    assert(popup_hit.value().local_y == 30);
    assert(
        popup_hit.value().trusted_presentation ==
        os::display::TrustedPresentation::none);
    assert(compositor.validate_input_hit(popup_hit.value()));

    // A surface whose presented buffer was revoked cannot keep receiving input
    // for pixels the compositor no longer considers presented. Routing falls
    // through to the framed application root underneath it, and the old hit is
    // explicitly stale if a transport tries to deliver it later.
    compositor.invalidate_buffer(buffer_for(popup.value().id));
    auto stale_popup = compositor.validate_input_hit(popup_hit.value());
    assert(!stale_popup);
    expect_error(stale_popup.error(), os::display::errors::stale_input_hit);

    auto app_hit = compositor.hit_test_input(160, 130);
    assert(app_hit);
    assert(app_hit.value().surface == app_surface.value().id);
    assert(app_hit.value().owner == app);
    assert(app_hit.value().frame_sequence == 3U);
    assert(app_hit.value().local_x == 160);
    assert(app_hit.value().local_y == 130);
    assert(compositor.validate_input_hit(app_hit.value()));

    // A newly presented frame invalidates an event targeted to the old frame.
    // This closes the hit-test -> IPC-delivery TOCTOU window without requiring
    // a permanent input lock over the compositor scene.
    show_and_frame(compositor, app, app_surface.value(), 4U);
    auto stale_frame = compositor.validate_input_hit(app_hit.value());
    assert(!stale_frame);
    expect_error(stale_frame.error(), os::display::errors::stale_input_hit);

    // Global points not owned by any input-eligible framed surface fail closed.
    assert(compositor.set_visibility(
        app,
        app_surface.value().id,
        os::display::SurfaceVisibility::hidden));
    auto no_target = compositor.hit_test_input(160, 130);
    assert(!no_target);
    expect_error(no_target.error(), os::display::errors::unknown_surface);

    return 0;
}
