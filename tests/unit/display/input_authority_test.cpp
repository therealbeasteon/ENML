#include <cassert>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/display/error.hpp>
#include <os/display/input_bridge.hpp>

namespace {

constexpr os::core::PrincipalId app_principal{0x4150500000000101ULL, 1U};
constexpr os::core::PrincipalId shell_principal{0x5348454C4C000101ULL, 1U};
constexpr os::core::PrincipalId secure_principal{0x5345435552450101ULL, 1U};
constexpr os::core::PrincipalId input_principal{0x494E505554000101ULL, 1U};

constexpr os::core::PeerIdentity app{
    app_principal,
    os::core::UserId{7U},
    os::core::ProcessId{101U},
};
constexpr os::core::PeerIdentity input_service{
    input_principal,
    os::core::UserId{0U},
    os::core::ProcessId{501U},
};

void expect_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

} // namespace

int main() {
    os::display::Compositor compositor{
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        {.shell = shell_principal, .secure_ui = secure_principal},
    };
    assert(compositor.valid());

    auto surface = compositor.create_surface(app, {
        .role = os::display::SurfaceRole::application,
        .bounds = {50, 70, 400U, 600U},
        .accepts_input = true,
    });
    assert(surface);
    assert(compositor.set_visibility(
        app,
        surface.value().id,
        os::display::SurfaceVisibility::visible));
    os::display::FrameSubmission submission{
        .surface = surface.value().id,
        .buffer = os::display::BufferId{0x1001U},
        .sequence = 9U,
        .buffer_slot = 0U,
        .damage_count = 1U,
    };
    submission.damage[0] = {0, 0, 400U, 600U};
    assert(compositor.submit_frame(app, submission, 1'000'000U));

    os::display::InputBridgeAuthority authority{compositor, input_principal};
    assert(authority.valid());

    // An ordinary application cannot ask the compositor for authoritative
    // global-scene input targeting, even for its own pixels.
    auto app_attempt = authority.hit_test(app, 100, 120);
    assert(!app_attempt);
    expect_error(app_attempt.error(), os::display::errors::input_authority_denied);

    auto hit = authority.hit_test(input_service, 100, 120);
    assert(hit);
    assert(hit.value().surface == surface.value().id);
    assert(hit.value().owner == app);
    assert(hit.value().frame_sequence == 9U);
    assert(hit.value().local_x == 50);
    assert(hit.value().local_y == 50);
    assert(authority.validate_before_delivery(input_service, hit.value()));

    auto app_validate = authority.validate_before_delivery(app, hit.value());
    assert(!app_validate);
    expect_error(app_validate.error(), os::display::errors::input_authority_denied);

    // A transport cannot modify target identity and then rely on the trusted
    // caller credential to make the forged hit valid.
    auto forged = hit.value();
    forged.owner = input_service;
    auto forged_result = authority.validate_before_delivery(input_service, forged);
    assert(!forged_result);
    expect_error(forged_result.error(), os::display::errors::stale_input_hit);

    // Frame advancement invalidates the old target before delivery.
    submission.sequence = 10U;
    assert(compositor.submit_frame(app, submission, 2'000'000U));
    auto stale = authority.validate_before_delivery(input_service, hit.value());
    assert(!stale);
    expect_error(stale.error(), os::display::errors::stale_input_hit);

    os::display::InputBridgeAuthority invalid_authority{compositor, {}};
    assert(!invalid_authority.valid());
    auto invalid = invalid_authority.hit_test(input_service, 100, 120);
    assert(!invalid);
    expect_error(invalid.error(), os::display::errors::invalid_configuration);

    return 0;
}
