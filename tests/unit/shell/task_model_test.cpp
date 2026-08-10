#include <os/shell/task_model.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/package/package.hpp>
#include <os/shell/error.hpp>

namespace {

void expect_shell_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::shell);
    assert(error.code == code);
}

os::package::ApplicationIdentity application_identity() {
    auto package = os::package::PackageId::parse("com.enml.shell.fixture");
    assert(package);
    os::package::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x53};
    return os::package::ApplicationIdentity{
        .package_id = package.value(),
        .signer_lineage = signer,
    };
}

os::core::PeerIdentity peer(std::uint64_t serial) {
    return os::core::PeerIdentity{
        .principal = os::core::PrincipalId{
            .high = 0x5348454C4C000000ULL,
            .low = serial,
        },
        .user = os::core::UserId{7U},
        .process = os::core::ProcessId{100U + serial},
    };
}

os::shell::ShellTask task(
    std::uint64_t serial,
    std::uint64_t display_generation = 9U) {
    return os::shell::ShellTask{
        .instance = os::core::ApplicationInstanceId{serial},
        .application = application_identity(),
        .owner = peer(serial),
        .root_surface = os::display::SurfaceId{
            os::display::make_display_object_value(
                display_generation,
                static_cast<std::uint32_t>(serial))},
    };
}

os::shell::ShellApplicationRecord application_record(std::uint64_t serial) {
    return os::shell::ShellApplicationRecord{
        .instance = os::core::ApplicationInstanceId{serial},
        .application = application_identity(),
        .owner = peer(serial),
    };
}

os::display::SceneEntry application_scene_entry(
    std::uint64_t serial,
    std::uint64_t display_generation = 9U) {
    return os::display::SceneEntry{
        .surface = os::display::SurfaceDescriptor{
            .id = os::display::SurfaceId{
                os::display::make_display_object_value(
                    display_generation,
                    static_cast<std::uint32_t>(serial))},
            .owner = peer(serial),
            .role = os::display::SurfaceRole::application,
            .bounds = os::display::Rect{0, 0, 240U, 480U},
            .visibility = os::display::SurfaceVisibility::visible,
            .accepts_input = true,
        },
        .trusted_presentation = os::display::TrustedPresentation::none,
    };
}

os::shell::ShellApplicationSnapshot lifecycle_snapshot(
    std::uint64_t revision,
    std::uint64_t first,
    std::uint64_t second = 0U) {
    os::shell::ShellApplicationSnapshot snapshot{};
    snapshot.revision = revision;
    snapshot.applications[0] = application_record(first);
    snapshot.count = 1U;
    if (second != 0U) {
        snapshot.applications[1] = application_record(second);
        snapshot.count = 2U;
    }
    return snapshot;
}

os::display::SceneSnapshot scene_snapshot(
    std::uint64_t first,
    std::uint64_t second = 0U,
    std::uint64_t display_generation = 9U) {
    os::display::SceneSnapshot scene{};
    scene.entries[0] = application_scene_entry(first, display_generation);
    scene.count = 1U;
    if (second != 0U) {
        scene.entries[1] = application_scene_entry(second, display_generation);
        scene.count = 2U;
    }
    return scene;
}

} // namespace

int main() {
    os::shell::ShellTaskModel model{};
    auto initial = model.snapshot();
    assert(initial.revision == os::shell::ShellRevision{1U});
    assert(initial.view == os::shell::ShellView::home);
    assert(initial.active_instance.value() == 0U);
    assert(initial.task_count == 0U);

    auto first = task(1U);
    assert(model.publish(first));
    auto published = model.snapshot();
    assert(published.revision == os::shell::ShellRevision{2U});
    assert(published.task_count == 1U);
    assert(published.tasks[0].activation_serial == 0U);

    // Idempotent publication does not churn a shell revision.
    assert(model.publish(first));
    assert(model.snapshot().revision == published.revision);

    // A compositor restart can replace only the generation-scoped root surface
    // for the same exact application instance/owner without creating a new task.
    auto recovered = task(1U, 10U);
    assert(model.publish(recovered));
    auto after_recovery = model.snapshot();
    assert(after_recovery.revision == os::shell::ShellRevision{3U});
    assert(after_recovery.task_count == 1U);
    assert(after_recovery.tasks[0].root_surface == recovered.root_surface);

    auto stale_direct = model.publish(task(1U, 9U));
    assert(!stale_direct);
    expect_shell_error(stale_direct.error(), os::shell::errors::stale_scene_snapshot);

    // An instance id cannot be rebound to another process identity.
    auto rebound = recovered;
    rebound.owner = peer(99U);
    auto conflict = model.publish(rebound);
    assert(!conflict);
    expect_shell_error(conflict.error(), os::shell::errors::task_conflict);

    auto second = task(2U);
    assert(model.publish(second));
    assert(model.activate(first.instance));
    auto active = model.snapshot();
    assert(active.view == os::shell::ShellView::application);
    assert(active.active_instance == first.instance);
    assert(active.tasks[0].activation_serial == 1U);

    assert(model.show_overview());
    auto overview = model.snapshot();
    assert(overview.view == os::shell::ShellView::overview);
    assert(overview.active_instance == first.instance);

    assert(model.activate(second.instance));
    auto second_active = model.snapshot();
    assert(second_active.view == os::shell::ShellView::application);
    assert(second_active.active_instance == second.instance);
    assert(second_active.tasks[1].activation_serial == 2U);

    // Removing the foreground app returns to home. The shell does not
    // heuristically promote another process simply because one exists.
    assert(model.remove(second.instance));
    auto after_remove = model.snapshot();
    assert(after_remove.view == os::shell::ShellView::home);
    assert(after_remove.active_instance.value() == 0U);
    assert(after_remove.task_count == 1U);

    auto unknown = model.remove(os::core::ApplicationInstanceId{999U});
    assert(!unknown);
    expect_shell_error(unknown.error(), os::shell::errors::unknown_task);

    // Caller-supplied recency is rejected; activation history is shell-owned.
    auto forged_recency = task(3U);
    forged_recency.activation_serial = 99U;
    auto invalid = model.publish(forged_recency);
    assert(!invalid);
    expect_shell_error(invalid.error(), os::shell::errors::invalid_task);

    // Fill the fixed task table. Multiple live instances of one signed app are
    // allowed only because they have distinct exact process/surface identities.
    for (std::uint64_t serial = 3U; serial <= 17U; ++serial) {
        assert(model.publish(task(serial)));
    }
    assert(model.snapshot().task_count == os::shell::max_shell_tasks);

    auto over_capacity = model.publish(task(18U));
    assert(!over_capacity);
    expect_shell_error(over_capacity.error(), os::shell::errors::task_capacity);

    // Duplicating a live owner or root surface under another instance is a
    // conflicting shell identity, not a second task.
    os::shell::ShellTaskModel conflict_model{};
    assert(conflict_model.publish(task(1U)));
    auto duplicate_owner = task(2U);
    duplicate_owner.owner = peer(1U);
    auto owner_conflict = conflict_model.publish(duplicate_owner);
    assert(!owner_conflict);
    expect_shell_error(owner_conflict.error(), os::shell::errors::task_conflict);

    auto duplicate_surface = task(2U);
    duplicate_surface.root_surface = task(1U).root_surface;
    auto surface_conflict = conflict_model.publish(duplicate_surface);
    assert(!surface_conflict);
    expect_shell_error(surface_conflict.error(), os::shell::errors::task_conflict);

    assert(conflict_model.show_home());
    assert(conflict_model.snapshot().view == os::shell::ShellView::home);

    // Reconciliation requires corroboration from two trusted owners: App
    // Manager lifecycle state and compositor application-root state. Neither
    // source can mint a complete shell task alone.
    os::shell::ShellTaskModel reconciled{};
    auto lifecycle = lifecycle_snapshot(1U, 1U, 2U);
    auto scene = scene_snapshot(1U, 2U);
    assert(reconciled.reconcile(lifecycle, scene));
    auto joined = reconciled.snapshot();
    assert(joined.task_count == 2U);
    assert(joined.tasks[0].instance == os::core::ApplicationInstanceId{1U});
    assert(joined.tasks[1].instance == os::core::ApplicationInstanceId{2U});
    assert(joined.tasks[0].owner == peer(1U));
    assert(joined.tasks[0].root_surface == scene.entries[0].surface.id);

    const auto joined_revision = joined.revision;

    // A newer lifecycle revision with identical joined task state is observed
    // without creating a shell-visible revision or depending on table order.
    auto reordered_lifecycle = lifecycle_snapshot(2U, 2U, 1U);
    assert(reconciled.reconcile(reordered_lifecycle, scene));
    auto unchanged = reconciled.snapshot();
    assert(unchanged.revision == joined_revision);
    assert(unchanged.tasks[0].instance == os::core::ApplicationInstanceId{1U});
    assert(unchanged.tasks[1].instance == os::core::ApplicationInstanceId{2U});

    // Lifecycle without a compositor root and a compositor root without
    // lifecycle ownership are both omitted instead of guessed into tasks.
    os::shell::ShellTaskModel lifecycle_only{};
    os::display::SceneSnapshot empty_scene{};
    assert(lifecycle_only.reconcile(lifecycle_snapshot(1U, 1U), empty_scene));
    assert(lifecycle_only.snapshot().task_count == 0U);

    os::shell::ShellTaskModel scene_only{};
    os::shell::ShellApplicationSnapshot empty_lifecycle{};
    empty_lifecycle.revision = 1U;
    assert(scene_only.reconcile(empty_lifecycle, scene_snapshot(1U)));
    assert(scene_only.snapshot().task_count == 0U);

    // A superficially similar application root owned by another exact process
    // does not join to the lifecycle entry.
    os::shell::ShellTaskModel owner_mismatch_model{};
    auto mismatched_scene = scene_snapshot(1U);
    mismatched_scene.entries[0].surface.owner = peer(99U);
    assert(owner_mismatch_model.reconcile(
        lifecycle_snapshot(1U, 1U), mismatched_scene));
    assert(owner_mismatch_model.snapshot().task_count == 0U);

    // Lifecycle identity and owner must each be unique in one coherent source.
    os::shell::ShellTaskModel invalid_lifecycle_model{};
    auto duplicate_lifecycle = lifecycle_snapshot(1U, 1U, 2U);
    duplicate_lifecycle.applications[1].instance = duplicate_lifecycle.applications[0].instance;
    auto duplicate_lifecycle_result = invalid_lifecycle_model.reconcile(
        duplicate_lifecycle, scene);
    assert(!duplicate_lifecycle_result);
    expect_shell_error(
        duplicate_lifecycle_result.error(),
        os::shell::errors::invalid_lifecycle_snapshot);

    auto duplicate_lifecycle_owner = lifecycle_snapshot(1U, 1U, 2U);
    duplicate_lifecycle_owner.applications[1].owner = duplicate_lifecycle_owner.applications[0].owner;
    auto duplicate_owner_result = invalid_lifecycle_model.reconcile(
        duplicate_lifecycle_owner, scene);
    assert(!duplicate_owner_result);
    expect_shell_error(
        duplicate_owner_result.error(),
        os::shell::errors::invalid_lifecycle_snapshot);

    // One exact owner cannot have two application roots in one compositor
    // snapshot. Such a scene contradicts the compositor invariant and fails.
    os::shell::ShellTaskModel duplicate_root_model{};
    auto duplicate_root_scene = scene_snapshot(1U);
    duplicate_root_scene.entries[1] = application_scene_entry(2U);
    duplicate_root_scene.entries[1].surface.owner = peer(1U);
    duplicate_root_scene.count = 2U;
    auto duplicate_root = duplicate_root_model.reconcile(
        lifecycle_snapshot(1U, 1U), duplicate_root_scene);
    assert(!duplicate_root);
    expect_shell_error(duplicate_root.error(), os::shell::errors::invalid_scene_snapshot);

    // Scene trust metadata must remain consistent with the compositor role.
    os::shell::ShellTaskModel invalid_trust_model{};
    auto invalid_trust_scene = scene_snapshot(1U);
    invalid_trust_scene.entries[0].trusted_presentation =
        os::display::TrustedPresentation::secure_system;
    auto invalid_trust = invalid_trust_model.reconcile(
        lifecycle_snapshot(1U, 1U), invalid_trust_scene);
    assert(!invalid_trust);
    expect_shell_error(invalid_trust.error(), os::shell::errors::invalid_scene_snapshot);

    // Popups and trusted system chrome can coexist in the scene but never
    // become tasks. Only exact application root surfaces participate in the
    // lifecycle join.
    os::shell::ShellTaskModel mixed_scene_model{};
    auto mixed_scene = scene_snapshot(1U);
    mixed_scene.entries[1] = os::display::SceneEntry{
        .surface = os::display::SurfaceDescriptor{
            .id = os::display::SurfaceId{
                os::display::make_display_object_value(9U, 90U)},
            .owner = peer(1U),
            .role = os::display::SurfaceRole::popup,
            .parent = mixed_scene.entries[0].surface.id,
            .bounds = os::display::Rect{10, 10, 100U, 100U},
            .visibility = os::display::SurfaceVisibility::visible,
            .accepts_input = true,
        },
        .trusted_presentation = os::display::TrustedPresentation::none,
    };
    mixed_scene.entries[2] = os::display::SceneEntry{
        .surface = os::display::SurfaceDescriptor{
            .id = os::display::SurfaceId{
                os::display::make_display_object_value(9U, 91U)},
            .owner = peer(50U),
            .role = os::display::SurfaceRole::system_chrome,
            .bounds = os::display::Rect{0, 0, 240U, 24U},
            .visibility = os::display::SurfaceVisibility::visible,
            .accepts_input = true,
        },
        .trusted_presentation = os::display::TrustedPresentation::system_chrome,
    };
    mixed_scene.count = 3U;
    assert(mixed_scene_model.reconcile(lifecycle_snapshot(1U, 1U), mixed_scene));
    assert(mixed_scene_model.snapshot().task_count == 1U);

    // Activation history survives a coherent compositor generation restart for
    // the same exact application identity.
    assert(reconciled.activate(os::core::ApplicationInstanceId{1U}));
    const auto activation_serial = reconciled.snapshot().tasks[0].activation_serial;
    auto restarted_scene = scene_snapshot(1U, 2U, 10U);
    assert(reconciled.reconcile(reordered_lifecycle, restarted_scene));
    auto after_scene_restart = reconciled.snapshot();
    assert(after_scene_restart.tasks[0].root_surface == restarted_scene.entries[0].surface.id);
    assert(after_scene_restart.tasks[0].activation_serial == activation_serial);

    // An older compositor root for the same exact live task is rejected after a
    // newer generation has already been accepted.
    auto stale_scene = reconciled.reconcile(reordered_lifecycle, scene);
    assert(!stale_scene);
    expect_shell_error(stale_scene.error(), os::shell::errors::stale_scene_snapshot);

    // Lifecycle revisions cannot move backwards after the shell has observed a
    // newer coherent snapshot.
    auto stale_lifecycle = reconciled.reconcile(lifecycle_snapshot(1U, 1U, 2U), restarted_scene);
    assert(!stale_lifecycle);
    expect_shell_error(
        stale_lifecycle.error(),
        os::shell::errors::stale_lifecycle_snapshot);

    // If the active instance disappears from the authoritative lifecycle join,
    // shell state returns to home rather than choosing another app implicitly.
    auto lifecycle_without_first = lifecycle_snapshot(3U, 2U);
    auto scene_without_first = scene_snapshot(2U, 0U, 10U);
    assert(reconciled.reconcile(lifecycle_without_first, scene_without_first));
    auto after_authoritative_removal = reconciled.snapshot();
    assert(after_authoritative_removal.task_count == 1U);
    assert(after_authoritative_removal.tasks[0].instance == os::core::ApplicationInstanceId{2U});
    assert(after_authoritative_removal.view == os::shell::ShellView::home);
    assert(after_authoritative_removal.active_instance.value() == 0U);

    return 0;
}
