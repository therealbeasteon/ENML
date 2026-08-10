#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/package/package.hpp>
#include <os/shell/error.hpp>
#include <os/shell/preview_policy.hpp>

namespace {

os::package::ApplicationIdentity application_identity() {
    auto package = os::package::PackageId::parse("com.enml.shell.preview");
    assert(package);
    os::package::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x51};
    assert(signer.valid());
    return os::package::ApplicationIdentity{
        .package_id = package.value(),
        .signer_lineage = signer,
    };
}

constexpr os::core::PeerIdentity owner{
    .principal = {0x5052455649455701ULL, 1U},
    .user = os::core::UserId{7U},
    .process = os::core::ProcessId{101U},
};

os::shell::ShellSnapshot shell_snapshot() {
    os::shell::ShellSnapshot shell{};
    shell.revision = os::shell::ShellRevision{4U};
    shell.view = os::shell::ShellView::overview;
    shell.task_count = 1U;
    shell.tasks[0] = os::shell::ShellTask{
        .instance = os::core::ApplicationInstanceId{9U},
        .application = application_identity(),
        .owner = owner,
        .root_surface = os::display::SurfaceId{
            os::display::make_display_object_value(12U, 3U)},
        .activation_serial = 2U,
    };
    return shell;
}

os::display::SceneSnapshot scene_snapshot(const os::shell::ShellSnapshot& shell) {
    os::display::SceneSnapshot scene{};
    scene.count = 1U;
    scene.entries[0] = os::display::SceneEntry{
        .surface = os::display::SurfaceDescriptor{
            .id = shell.tasks[0].root_surface,
            .owner = shell.tasks[0].owner,
            .role = os::display::SurfaceRole::application,
            .bounds = {0, 0, 1080U, 2400U},
            .visibility = os::display::SurfaceVisibility::visible,
            .accepts_input = true,
        },
        .buffer = os::display::BufferId{
            os::display::make_display_object_value(12U, 5U)},
        .frame_sequence = 7U,
        .buffer_slot = 1U,
        .has_frame = true,
        .capture_allowed = true,
        .trusted_presentation = os::display::TrustedPresentation::none,
    };
    return scene;
}

void expect_shell_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::shell);
    assert(error.code == code);
}

} // namespace

int main() {
    const auto shell = shell_snapshot();
    const auto scene = scene_snapshot(shell);

    auto grant = os::shell::authorize_task_preview(
        shell,
        scene,
        os::core::ApplicationInstanceId{9U});
    assert(grant);
    assert(grant.value().shell_revision == shell.revision);
    assert(grant.value().owner == owner);
    assert(grant.value().buffer == scene.entries[0].buffer);
    assert(grant.value().frame_sequence == 7U);
    assert(os::shell::validate_task_preview_grant(shell, scene, grant.value()));

    // Hidden tasks do not cause the shell to retain/capture a screenshot merely
    // to decorate recents. Overview can use semantic task cards instead.
    auto hidden_scene = scene;
    hidden_scene.entries[0].surface.visibility = os::display::SurfaceVisibility::hidden;
    auto hidden = os::shell::authorize_task_preview(
        shell, hidden_scene, os::core::ApplicationInstanceId{9U});
    assert(!hidden);
    expect_shell_error(hidden.error(), os::shell::errors::preview_capture_denied);

    // Compositor capture policy is authoritative. A non-capturable surface is
    // denied even if every other task field matches.
    auto capture_denied_scene = scene;
    capture_denied_scene.entries[0].capture_allowed = false;
    auto capture_denied = os::shell::authorize_task_preview(
        shell, capture_denied_scene, os::core::ApplicationInstanceId{9U});
    assert(!capture_denied);
    expect_shell_error(capture_denied.error(), os::shell::errors::preview_capture_denied);

    // Trusted-system presentation is never accepted as an ordinary task
    // preview input, even if malformed upstream state also says capture=true.
    auto trusted_scene = scene;
    trusted_scene.entries[0].trusted_presentation = os::display::TrustedPresentation::secure_system;
    auto trusted = os::shell::authorize_task_preview(
        shell, trusted_scene, os::core::ApplicationInstanceId{9U});
    assert(!trusted);
    expect_shell_error(trusted.error(), os::shell::errors::preview_capture_denied);

    // Popups are not task roots and cannot be substituted for a root preview.
    auto popup_scene = scene;
    popup_scene.entries[0].surface.role = os::display::SurfaceRole::popup;
    popup_scene.entries[0].surface.parent = shell.tasks[0].root_surface;
    auto popup = os::shell::authorize_task_preview(
        shell, popup_scene, os::core::ApplicationInstanceId{9U});
    assert(!popup);
    expect_shell_error(popup.error(), os::shell::errors::preview_capture_denied);

    // Any frame replacement makes the previously issued grant stale. The
    // capture implementation must obtain a fresh grant rather than sampling a
    // different frame under old semantic authorization.
    auto new_frame_scene = scene;
    new_frame_scene.entries[0].frame_sequence = 8U;
    auto stale_frame = os::shell::validate_task_preview_grant(
        shell, new_frame_scene, grant.value());
    assert(!stale_frame);
    expect_shell_error(stale_frame.error(), os::shell::errors::stale_preview_grant);

    // Shell state changes also invalidate the grant, preventing recents capture
    // from becoming independent authority after navigation/lifecycle mutation.
    auto newer_shell = shell;
    newer_shell.revision = os::shell::ShellRevision{5U};
    auto stale_shell = os::shell::validate_task_preview_grant(
        newer_shell, scene, grant.value());
    assert(!stale_shell);
    expect_shell_error(stale_shell.error(), os::shell::errors::stale_preview_grant);

    return 0;
}
