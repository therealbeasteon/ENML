#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/display/compositor.hpp>
#include <os/display/error.hpp>

namespace {

constexpr os::core::PrincipalId app_principal{
    0x4150500000000001ULL,
    0x0000000000000001ULL,
};
constexpr os::core::PrincipalId other_app_principal{
    0x4150500000000002ULL,
    0x0000000000000002ULL,
};
constexpr os::core::PrincipalId shell_principal{
    0x5348454C4C000001ULL,
    0x0000000000000001ULL,
};
constexpr os::core::PrincipalId secure_ui_principal{
    0x5345435552450001ULL,
    0x0000000000000001ULL,
};

constexpr os::core::PeerIdentity app_peer{
    app_principal,
    os::core::UserId{7U},
    os::core::ProcessId{101U},
};
constexpr os::core::PeerIdentity app_peer_new_process{
    app_principal,
    os::core::UserId{7U},
    os::core::ProcessId{102U},
};
constexpr os::core::PeerIdentity other_app_peer{
    other_app_principal,
    os::core::UserId{7U},
    os::core::ProcessId{201U},
};
constexpr os::core::PeerIdentity shell_peer{
    shell_principal,
    os::core::UserId{0U},
    os::core::ProcessId{301U},
};
constexpr os::core::PeerIdentity secure_peer{
    secure_ui_principal,
    os::core::UserId{0U},
    os::core::ProcessId{401U},
};

os::display::Compositor make_compositor() {
    return os::display::Compositor{
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .safe_insets = {.top = 80U, .right = 0U, .bottom = 100U, .left = 0U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        os::display::TrustedUiPrincipals{
            .shell = shell_principal,
            .secure_ui = secure_ui_principal,
        },
    };
}

os::display::SurfaceDescriptor create_app_surface(
    os::display::Compositor& compositor,
    os::core::PeerIdentity peer,
    os::display::Rect bounds = {0, 0, 1080U, 2400U}) {
    auto created = compositor.create_surface(
        peer,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::application,
            .parent = {},
            .bounds = bounds,
            .accepts_input = true,
        });
    assert(created);
    return created.value();
}

void submit_full_frame(
    os::display::Compositor& compositor,
    os::core::PeerIdentity peer,
    os::display::SurfaceId surface,
    std::uint64_t sequence,
    std::uint8_t slot,
    std::uint32_t width,
    std::uint32_t height) {
    os::display::FrameSubmission submission{
        .surface = surface,
        .sequence = sequence,
        .buffer_slot = slot,
        .damage_count = 1U,
    };
    submission.damage[0] = os::display::Rect{0, 0, width, height};
    auto submitted = compositor.submit_frame(peer, submission, 1'000'000U);
    assert(submitted);
}

void expect_display_error(
    const os::core::Error& error,
    std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

} // namespace

int main() {
    {
        os::display::Compositor invalid{
            os::display::DisplayConfiguration{
                .size = {1080U, 2400U},
                .safe_insets = {.top = 1200U, .right = 0U, .bottom = 1200U, .left = 0U},
                .refresh_millihz = 60'000U,
                .compositor_margin_ns = 1'000'000U,
            },
            os::display::TrustedUiPrincipals{
                .shell = shell_principal,
                .secure_ui = secure_ui_principal,
            },
        };
        assert(!invalid.valid());
    }

    auto compositor = make_compositor();
    assert(compositor.valid());
    assert(compositor.configuration().size == os::display::PixelSize{1080U, 2400U});

    const auto app = create_app_surface(compositor, app_peer);
    assert(app.valid());
    assert(app.visibility == os::display::SurfaceVisibility::hidden);
    assert(app.owner == app_peer);

    // Applications cannot self-assign trusted system or secure surface roles.
    auto forged_chrome = compositor.create_surface(
        app_peer,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::system_chrome,
            .bounds = {0, 0, 1080U, 120U},
        });
    assert(!forged_chrome);
    expect_display_error(forged_chrome.error(), os::display::errors::invalid_role);

    auto forged_secure = compositor.create_surface(
        shell_peer,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::secure_system,
            .bounds = {100, 500, 880U, 700U},
        });
    assert(!forged_secure);
    expect_display_error(forged_secure.error(), os::display::errors::invalid_role);

    auto chrome_result = compositor.create_surface(
        shell_peer,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::system_chrome,
            .bounds = {0, 0, 1080U, 160U},
            .accepts_input = true,
        });
    assert(chrome_result);
    const auto chrome = chrome_result.value();

    auto secure_result = compositor.create_surface(
        secure_peer,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::secure_system,
            .bounds = {100, 400, 880U, 800U},
            .accepts_input = true,
        });
    assert(secure_result);
    const auto secure = secure_result.value();

    // Popups are subordinate to an application surface owned by exactly the
    // same process identity. A same-principal replacement process does not
    // inherit the old process's UI authority.
    auto popup_result = compositor.create_surface(
        app_peer,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::popup,
            .parent = app.id,
            .bounds = {150, 350, 780U, 500U},
            .accepts_input = true,
        });
    assert(popup_result);
    const auto popup = popup_result.value();

    auto stolen_popup = compositor.create_surface(
        app_peer_new_process,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::popup,
            .parent = app.id,
            .bounds = {200, 400, 500U, 400U},
        });
    assert(!stolen_popup);
    expect_display_error(stolen_popup.error(), os::display::errors::invalid_parent);

    auto out_of_bounds = compositor.set_bounds(app_peer, app.id, {0, 0, 1081U, 2400U});
    assert(!out_of_bounds);
    expect_display_error(out_of_bounds.error(), os::display::errors::invalid_geometry);

    auto stolen_modify = compositor.set_visibility(
        other_app_peer,
        app.id,
        os::display::SurfaceVisibility::visible);
    assert(!stolen_modify);
    expect_display_error(stolen_modify.error(), os::display::errors::owner_mismatch);

    assert(compositor.set_visibility(app_peer, app.id, os::display::SurfaceVisibility::visible));
    assert(compositor.set_visibility(app_peer, popup.id, os::display::SurfaceVisibility::visible));
    assert(compositor.set_visibility(shell_peer, chrome.id, os::display::SurfaceVisibility::visible));
    assert(compositor.set_visibility(secure_peer, secure.id, os::display::SurfaceVisibility::visible));

    submit_full_frame(compositor, app_peer, app.id, 1U, 0U, 1080U, 2400U);
    submit_full_frame(compositor, app_peer, popup.id, 1U, 1U, 780U, 500U);
    submit_full_frame(compositor, shell_peer, chrome.id, 1U, 0U, 1080U, 160U);
    submit_full_frame(compositor, secure_peer, secure.id, 1U, 2U, 880U, 800U);

    // Sequence replay and out-of-range buffer slots fail before changing state.
    os::display::FrameSubmission replay{
        .surface = app.id,
        .sequence = 1U,
        .buffer_slot = 1U,
        .damage_count = 0U,
    };
    auto replayed = compositor.submit_frame(app_peer, replay, 2'000'000U);
    assert(!replayed);
    expect_display_error(replayed.error(), os::display::errors::frame_replay);

    os::display::FrameSubmission bad_slot{
        .surface = app.id,
        .sequence = 2U,
        .buffer_slot = os::display::max_frame_buffer_slots,
        .damage_count = 0U,
    };
    auto bad_slot_result = compositor.submit_frame(app_peer, bad_slot, 2'000'000U);
    assert(!bad_slot_result);
    expect_display_error(bad_slot_result.error(), os::display::errors::invalid_buffer_slot);

    os::display::FrameSubmission bad_damage{
        .surface = app.id,
        .sequence = 2U,
        .buffer_slot = 1U,
        .damage_count = 1U,
    };
    bad_damage.damage[0] = {1000, 0, 200U, 100U};
    auto bad_damage_result = compositor.submit_frame(app_peer, bad_damage, 2'000'000U);
    assert(!bad_damage_result);
    expect_display_error(bad_damage_result.error(), os::display::errors::invalid_damage);

    os::display::FrameSubmission next_frame{
        .surface = app.id,
        .sequence = 2U,
        .buffer_slot = 1U,
        .damage_count = 0U,
    };
    auto next = compositor.submit_frame(app_peer, next_frame, 1'000'000U);
    assert(next);
    assert(next.value().deadline.next_vsync_ns == 16'666'666U);
    assert(next.value().deadline.submission_deadline_ns == 15'666'666U);

    // Global z authority is compositor-owned by role band, not app-supplied.
    const auto scene = compositor.scene_snapshot();
    assert(scene.count == 4U);
    assert(scene.entries[0].surface.id == app.id);
    assert(scene.entries[1].surface.id == popup.id);
    assert(scene.entries[2].surface.id == chrome.id);
    assert(scene.entries[3].surface.id == secure.id);
    assert(scene.entries[0].capture_allowed);
    assert(scene.entries[1].capture_allowed);
    assert(scene.entries[2].capture_allowed);
    assert(!scene.entries[3].capture_allowed);

    // Secure system UI wins input routing where it overlaps lower trust roles.
    auto secure_hit = compositor.hit_test(200, 500);
    assert(secure_hit && secure_hit.value() == secure.id);
    assert(compositor.set_visibility(
        secure_peer,
        secure.id,
        os::display::SurfaceVisibility::hidden));
    auto popup_hit = compositor.hit_test(200, 500);
    assert(popup_hit && popup_hit.value() == popup.id);

    // A geometry mutation requires fresh pixels before the surface can receive
    // hit-tested input at its new geometry.
    assert(compositor.set_bounds(app_peer, app.id, {0, 0, 1000U, 2200U}));
    auto no_app_frame_hit = compositor.hit_test(900, 2000);
    assert(!no_app_frame_hit);
    expect_display_error(no_app_frame_hit.error(), os::display::errors::unknown_surface);

    // Process death revokes exactly that execution's surfaces. Durable
    // PrincipalId continuity does not make a new ProcessId owner of old UI.
    compositor.revoke_process(app_peer.process);
    assert(compositor.surface_count() == 2U);
    assert(compositor.surface_count_for(app_principal) == 0U);
    auto stale = compositor.lookup(app.id);
    assert(!stale);
    expect_display_error(stale.error(), os::display::errors::unknown_surface);

    auto replacement = create_app_surface(compositor, app_peer_new_process, {0, 160, 1080U, 2140U});
    assert(replacement.owner.process == app_peer_new_process.process);

    // Per-principal quota is bounded independently of the global surface table.
    std::array<os::display::SurfaceId, os::display::max_surfaces_per_principal - 1U> quota_surfaces{};
    for (std::size_t index = 0U; index < quota_surfaces.size(); ++index) {
        auto extra = compositor.create_surface(
            app_peer_new_process,
            os::display::CreateSurfaceRequest{
                .role = os::display::SurfaceRole::application,
                .bounds = {0, 160, 200U, 200U},
            });
        assert(extra);
        quota_surfaces[index] = extra.value().id;
    }
    assert(compositor.surface_count_for(app_principal) == os::display::max_surfaces_per_principal);
    auto over_quota = compositor.create_surface(
        app_peer_new_process,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::application,
            .bounds = {0, 160, 200U, 200U},
        });
    assert(!over_quota);
    expect_display_error(over_quota.error(), os::display::errors::principal_surface_limit);

    // A different application principal retains its independent quota.
    auto independent = create_app_surface(compositor, other_app_peer, {0, 160, 300U, 300U});
    assert(independent.owner.principal == other_app_principal);

    // Parent destruction deterministically destroys directly-attached popup.
    auto parent = create_app_surface(compositor, other_app_peer, {0, 160, 400U, 400U});
    auto child = compositor.create_surface(
        other_app_peer,
        os::display::CreateSurfaceRequest{
            .role = os::display::SurfaceRole::popup,
            .parent = parent.id,
            .bounds = {20, 200, 200U, 120U},
        });
    assert(child);
    const auto before_destroy = compositor.surface_count();
    assert(compositor.destroy_surface(other_app_peer, parent.id));
    assert(compositor.surface_count() == before_destroy - 2U);
    assert(!compositor.lookup(child.value().id));

    return 0;
}
