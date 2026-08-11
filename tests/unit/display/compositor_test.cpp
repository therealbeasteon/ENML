#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/display/compositor.hpp>
#include <os/display/error.hpp>

namespace {

constexpr os::core::PrincipalId app_principal{0x4150500000000001ULL, 1U};
constexpr os::core::PrincipalId other_principal{0x4150500000000002ULL, 2U};
constexpr os::core::PrincipalId shell_principal{0x5348454C4C000001ULL, 1U};
constexpr os::core::PrincipalId secure_principal{0x5345435552450001ULL, 1U};

constexpr os::core::PeerIdentity app{app_principal, os::core::UserId{7U}, os::core::ProcessId{101U}};
constexpr os::core::PeerIdentity app_new{app_principal, os::core::UserId{7U}, os::core::ProcessId{102U}};
constexpr os::core::PeerIdentity other{other_principal, os::core::UserId{7U}, os::core::ProcessId{201U}};
constexpr os::core::PeerIdentity shell{shell_principal, os::core::UserId{0U}, os::core::ProcessId{301U}};
constexpr os::core::PeerIdentity secure{secure_principal, os::core::UserId{0U}, os::core::ProcessId{401U}};

os::display::Compositor make_compositor() {
    return {
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .safe_insets = {.top = 80U, .right = 0U, .bottom = 100U, .left = 0U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        {.shell = shell_principal, .secure_ui = secure_principal},
    };
}

void expect_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

os::display::BufferId buffer_for(os::display::SurfaceId surface) {
    return os::display::BufferId{surface.value() + 1000U};
}

os::display::SurfaceDescriptor create_app(
    os::display::Compositor& compositor,
    os::core::PeerIdentity peer,
    os::display::Rect bounds = {0, 0, 1080U, 2400U}) {
    auto result = compositor.create_surface(peer, {
        .role = os::display::SurfaceRole::application,
        .bounds = bounds,
        .accepts_input = true,
    });
    assert(result);
    return result.value();
}

os::display::SurfaceDescriptor create_role(
    os::display::Compositor& compositor,
    os::core::PeerIdentity peer,
    os::display::SurfaceRole role,
    os::display::Rect bounds,
    os::display::SurfaceId parent = {}) {
    auto result = compositor.create_surface(peer, {
        .role = role,
        .parent = parent,
        .bounds = bounds,
        .accepts_input = true,
    });
    assert(result);
    return result.value();
}

void show_and_frame(
    os::display::Compositor& compositor,
    os::core::PeerIdentity peer,
    const os::display::SurfaceDescriptor& surface,
    std::uint8_t slot = 0U) {
    assert(compositor.set_visibility(peer, surface.id, os::display::SurfaceVisibility::visible));
    os::display::FrameSubmission frame{
        .surface = surface.id,
        .buffer = buffer_for(surface.id),
        .sequence = 1U,
        .buffer_slot = slot,
        .damage_count = 1U,
    };
    frame.damage[0] = {0, 0, surface.bounds.width, surface.bounds.height};
    assert(compositor.submit_frame(peer, frame, 1'000'000U));
}

} // namespace

int main() {
    {
        os::display::Compositor invalid{
            os::display::DisplayConfiguration{
                .size = {1080U, 2400U},
                .safe_insets = {.top = 1200U, .bottom = 1200U},
                .refresh_millihz = 60'000U,
                .compositor_margin_ns = 1'000'000U,
            },
            {.shell = shell_principal, .secure_ui = secure_principal},
        };
        assert(!invalid.valid());
    }

    auto compositor = make_compositor();
    assert(compositor.valid());
    const os::display::PixelSize expected_size{1080U, 2400U};
    assert(compositor.configuration().size == expected_size);

    const auto first = create_app(compositor, app);
    auto duplicate_root = compositor.create_surface(app, {
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 1080U, 2400U},
    });
    assert(!duplicate_root);
    expect_error(duplicate_root.error(), os::display::errors::application_surface_exists);

    auto forged_chrome = compositor.create_surface(app, {
        .role = os::display::SurfaceRole::system_chrome,
        .bounds = {0, 0, 1080U, 160U},
    });
    assert(!forged_chrome);
    expect_error(forged_chrome.error(), os::display::errors::invalid_role);

    auto forged_secure = compositor.create_surface(shell, {
        .role = os::display::SurfaceRole::secure_system,
        .bounds = {100, 400, 880U, 800U},
    });
    assert(!forged_secure);
    expect_error(forged_secure.error(), os::display::errors::invalid_role);

    const auto popup = create_role(
        compositor, app, os::display::SurfaceRole::popup, {150, 350, 780U, 500U}, first.id);
    const auto second = create_app(compositor, other);
    const auto chrome = create_role(
        compositor, shell, os::display::SurfaceRole::system_chrome, {0, 0, 1080U, 160U});
    const auto secure_surface = create_role(
        compositor, secure, os::display::SurfaceRole::secure_system, {100, 400, 880U, 800U});

    auto stolen_popup = compositor.create_surface(app_new, {
        .role = os::display::SurfaceRole::popup,
        .parent = first.id,
        .bounds = {200, 400, 500U, 400U},
    });
    assert(!stolen_popup);
    expect_error(stolen_popup.error(), os::display::errors::invalid_parent);

    auto stolen_modify = compositor.set_visibility(other, first.id, os::display::SurfaceVisibility::visible);
    assert(!stolen_modify);
    expect_error(stolen_modify.error(), os::display::errors::owner_mismatch);

    auto self_raise = compositor.activate_application(app, first.id);
    assert(!self_raise);
    expect_error(self_raise.error(), os::display::errors::activation_denied);

    show_and_frame(compositor, app, first);
    show_and_frame(compositor, app, popup, 1U);
    show_and_frame(compositor, other, second);
    show_and_frame(compositor, shell, chrome);
    show_and_frame(compositor, secure, secure_surface, 2U);

    auto scene = compositor.scene_snapshot();
    assert(scene.count == 5U);
    assert(scene.entries[0].surface.id == first.id);
    assert(scene.entries[1].surface.id == popup.id);
    assert(scene.entries[2].surface.id == second.id);
    assert(scene.entries[3].surface.id == chrome.id);
    assert(scene.entries[4].surface.id == secure_surface.id);
    assert(scene.entries[0].buffer == buffer_for(first.id));

    // Trust attribution is compositor-derived metadata. Application and popup
    // pixels cannot mint it; only compositor-authorized system roles receive a
    // trusted presentation classification.
    assert(scene.entries[0].trusted_presentation == os::display::TrustedPresentation::none);
    assert(scene.entries[1].trusted_presentation == os::display::TrustedPresentation::none);
    assert(scene.entries[2].trusted_presentation == os::display::TrustedPresentation::none);
    assert(
        scene.entries[3].trusted_presentation ==
        os::display::TrustedPresentation::system_chrome);
    assert(
        scene.entries[4].trusted_presentation ==
        os::display::TrustedPresentation::secure_system);

    assert(compositor.set_visibility(secure, secure_surface.id, os::display::SurfaceVisibility::hidden));
    auto hit = compositor.hit_test(200, 500);
    assert(hit && hit.value() == second.id);

    assert(compositor.activate_application(shell, first.id));
    scene = compositor.scene_snapshot();
    assert(scene.entries[0].surface.id == second.id);
    assert(scene.entries[1].surface.id == first.id);
    assert(scene.entries[2].surface.id == popup.id);
    hit = compositor.hit_test(200, 500);
    assert(hit && hit.value() == popup.id);

    // Capture is an allow-list over roles, not a single exclusion. Only
    // application surfaces are capturable; popup, system chrome and
    // secure-system are all denied, matching M4.0's rule that none of them is
    // an ordinary task-preview input.
    assert(scene.entries[0].capture_allowed && scene.entries[1].capture_allowed);
    assert(!scene.entries[2].capture_allowed);
    assert(!scene.entries[3].capture_allowed);
    assert(!scene.entries[4].capture_allowed);

    // A default-constructed entry denies capture. SceneSnapshot builds its
    // whole array by default construction, so this is what keeps unused slots
    // and any future producer that forgets the field from reporting a
    // capturable surface.
    const os::display::SceneEntry defaulted{};
    assert(!defaulted.capture_allowed);
    assert(
        scene.entries[4].trusted_presentation ==
        os::display::TrustedPresentation::secure_system);

    auto out_of_bounds = compositor.set_bounds(app, first.id, {0, 0, 1081U, 2400U});
    assert(!out_of_bounds);
    expect_error(out_of_bounds.error(), os::display::errors::invalid_geometry);

    os::display::FrameSubmission missing_buffer{.surface = first.id, .sequence = 2U};
    auto missing_buffer_result = compositor.submit_frame(app, missing_buffer, 2'000'000U);
    assert(!missing_buffer_result);
    expect_error(missing_buffer_result.error(), os::display::errors::invalid_buffer);

    os::display::FrameSubmission replay{
        .surface = first.id,
        .buffer = buffer_for(first.id),
        .sequence = 1U,
        .buffer_slot = 1U,
    };
    auto replay_result = compositor.submit_frame(app, replay, 2'000'000U);
    assert(!replay_result);
    expect_error(replay_result.error(), os::display::errors::frame_replay);

    os::display::FrameSubmission bad_slot{
        .surface = first.id,
        .buffer = buffer_for(first.id),
        .sequence = 2U,
        .buffer_slot = os::display::max_frame_buffer_slots,
    };
    auto bad_slot_result = compositor.submit_frame(app, bad_slot, 2'000'000U);
    assert(!bad_slot_result);
    expect_error(bad_slot_result.error(), os::display::errors::invalid_buffer_slot);

    os::display::FrameSubmission bad_damage{
        .surface = first.id,
        .buffer = buffer_for(first.id),
        .sequence = 2U,
        .buffer_slot = 1U,
        .damage_count = 1U,
    };
    bad_damage.damage[0] = {1000, 0, 200U, 100U};
    auto bad_damage_result = compositor.submit_frame(app, bad_damage, 2'000'000U);
    assert(!bad_damage_result);
    expect_error(bad_damage_result.error(), os::display::errors::invalid_damage);

    os::display::FrameSubmission second_frame{
        .surface = first.id,
        .buffer = os::display::BufferId{2001U},
        .sequence = 2U,
        .buffer_slot = 1U,
    };
    auto timing = compositor.submit_frame(app, second_frame, 1'000'000U);
    assert(timing);
    assert(timing.value().buffer == second_frame.buffer);
    assert(timing.value().deadline.next_vsync_ns == 16'666'666U);
    assert(timing.value().deadline.submission_deadline_ns == 15'666'666U);

    compositor.invalidate_buffer(second_frame.buffer);
    scene = compositor.scene_snapshot();
    bool found_invalidated = false;
    for (std::size_t index = 0U; index < scene.count; ++index) {
        if (scene.entries[index].surface.id == first.id) {
            assert(!scene.entries[index].has_frame);
            assert(scene.entries[index].buffer.value() == 0U);
            found_invalidated = true;
        }
    }
    assert(found_invalidated);

    assert(compositor.set_bounds(app, first.id, {0, 0, 1000U, 2200U}));
    compositor.revoke_process(app.process);
    assert(!compositor.lookup(first.id));
    assert(!compositor.lookup(popup.id));
    assert(compositor.surface_count_for(app_principal) == 0U);

    const auto replacement = create_app(compositor, app_new, {0, 160, 1080U, 2140U});
    std::array<os::display::SurfaceId, os::display::max_surfaces_per_principal - 1U> quota{};
    for (std::size_t index = 0U; index < quota.size(); ++index) {
        const std::int32_t offset = static_cast<std::int32_t>(index * 5U);
        const auto extra = create_role(
            compositor, app_new, os::display::SurfaceRole::popup,
            {offset, 200 + offset, 200U, 120U}, replacement.id);
        quota[index] = extra.id;
    }
    auto over_quota = compositor.create_surface(app_new, {
        .role = os::display::SurfaceRole::popup,
        .parent = replacement.id,
        .bounds = {0, 200, 200U, 120U},
    });
    assert(!over_quota);
    expect_error(over_quota.error(), os::display::errors::principal_surface_limit);

    const auto other_popup = create_role(
        compositor, other, os::display::SurfaceRole::popup, {20, 200, 200U, 120U}, second.id);
    const auto before = compositor.surface_count();
    assert(compositor.destroy_surface(other, second.id));
    assert(compositor.surface_count() == before - 2U);
    assert(!compositor.lookup(other_popup.id));
    assert(compositor.lookup(replacement.id));
    return 0;
}
