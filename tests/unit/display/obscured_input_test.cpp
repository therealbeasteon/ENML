#include <cassert>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/display/compositor.hpp>
#include <os/display/error.hpp>

// Input must not fall through a surface the user can see.
//
// Hit testing walks the scene from the top and delivers to the first surface
// that accepts input. A surface above it that draws pixels but declines input
// is skipped - so the user aims at one principal's pixels and the event arrives
// at another principal's surface. That is the whole of a tapjacking attack: the
// attacker never needs to read anything, only to be believed.
//
// The compositor refuses such a hit rather than reporting it. A caller handed
// "here is a hit, but it may be a lie" will use it.

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
    };
    assert(compositor.submit_frame(owner, submission));
}

} // namespace

int main() {
    os::display::Compositor compositor{
        {
            .size = {1080U, 2400U},
            .scale_percent = 100U,
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
    show_and_frame(compositor, app, app_surface.value(), 3U);

    // With nothing above it, the application receives the event.
    {
        auto hit = compositor.hit_test_input(500, 900);
        assert(hit);
        assert(hit.value().surface == app_surface.value().id);
        assert(!hit.value().obscured_at_point);
        assert(!hit.value().partially_obscured);
        assert(compositor.validate_input_hit(hit.value()));
    }

    // The application's own surface layered over itself is not deception. A
    // principal composing its interface from several surfaces is not lying to
    // itself, and treating that as an attack would make layering impossible.
    auto own_overlay = compositor.create_surface(app, {
        .role = os::display::SurfaceRole::popup,
        .parent = app_surface.value().id,
        .bounds = {400, 800, 300U, 200U},
        .accepts_input = false,
    });
    assert(own_overlay);
    show_and_frame(compositor, app, own_overlay.value(), 4U);
    {
        auto hit = compositor.hit_test_input(500, 900);
        assert(hit);
        assert(hit.value().surface == app_surface.value().id);
        assert(compositor.validate_input_hit(hit.value()));
    }

    // Another principal's surface, visible and holding a frame but declining
    // input, covering the same point. Hit testing would previously have skipped
    // it and delivered to the application underneath - the user pressing what
    // they saw, the event arriving somewhere else.
    auto other_overlay = compositor.create_surface(secure, {
        .role = os::display::SurfaceRole::secure_system,
        .bounds = {450, 850, 200U, 160U},
        .accepts_input = false,
    });
    assert(other_overlay);
    show_and_frame(compositor, secure, other_overlay.value(), 5U);

    {
        auto covered = compositor.hit_test_input(500, 900);
        assert(!covered);
        expect_error(covered.error(), os::display::errors::obscured_input);
    }

    // The refusal is not indiscriminate: it must be the covering that matters.
    // A point far from the overlay is still delivered - except that this
    // overlay overlaps the full-screen application surface, so the target is
    // partially obscured wherever it is touched. That is deliberate: an overlay
    // covering part of a surface can have changed what the user believed they
    // were pressing, for instance by hiding the label beside a control.
    {
        auto elsewhere = compositor.hit_test_input(50, 50);
        assert(!elsewhere);
        expect_error(elsewhere.error(), os::display::errors::obscured_input);
    }

    // Once the covering surface is hidden, delivery resumes. The refusal is a
    // property of the scene at the moment of the event, not a latch.
    assert(compositor.set_visibility(
        secure,
        other_overlay.value().id,
        os::display::SurfaceVisibility::hidden));
    auto clear_hit = compositor.hit_test_input(500, 900);
    assert(clear_hit);
    assert(clear_hit.value().surface == app_surface.value().id);

    // The time-of-check window. The event was taken while nothing covered the
    // target; the overlay is raised before it is delivered. Nothing about the
    // target changed - same bounds, same frame sequence - so every other
    // staleness check still passes. Validation must re-derive the covering from
    // the current scene rather than trust the flags the hit carries.
    assert(compositor.set_visibility(
        secure,
        other_overlay.value().id,
        os::display::SurfaceVisibility::visible));
    auto raced = compositor.validate_input_hit(clear_hit.value());
    assert(!raced);
    expect_error(raced.error(), os::display::errors::obscured_input);

    // A hit carrying the flags was never produced by this compositor, and must
    // be refused if one is ever presented.
    assert(compositor.set_visibility(
        secure,
        other_overlay.value().id,
        os::display::SurfaceVisibility::hidden));
    auto forged = clear_hit.value();
    forged.obscured_at_point = true;
    auto rejected = compositor.validate_input_hit(forged);
    assert(!rejected);
    expect_error(rejected.error(), os::display::errors::obscured_input);

    return 0;
}
