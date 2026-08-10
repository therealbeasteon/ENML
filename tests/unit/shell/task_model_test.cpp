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

os::shell::ShellTask task(std::uint64_t serial) {
    return os::shell::ShellTask{
        .instance = os::core::ApplicationInstanceId{serial},
        .application = application_identity(),
        .owner = peer(serial),
        .root_surface = os::display::SurfaceId{
            os::display::make_display_object_value(9U, static_cast<std::uint32_t>(serial))},
    };
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
    auto recovered = first;
    recovered.root_surface = os::display::SurfaceId{
        os::display::make_display_object_value(10U, 1U)};
    assert(model.publish(recovered));
    auto after_recovery = model.snapshot();
    assert(after_recovery.revision == os::shell::ShellRevision{3U});
    assert(after_recovery.task_count == 1U);
    assert(after_recovery.tasks[0].root_surface == recovered.root_surface);

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
    return 0;
}
