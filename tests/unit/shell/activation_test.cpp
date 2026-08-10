#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/package/package.hpp>
#include <os/shell/activation.hpp>
#include <os/shell/error.hpp>

namespace {

os::package::ApplicationIdentity application_identity() {
    auto package = os::package::PackageId::parse("com.enml.shell.activation");
    assert(package);
    os::package::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x71};
    assert(signer.valid());
    return os::package::ApplicationIdentity{
        .package_id = package.value(),
        .signer_lineage = signer,
    };
}

constexpr os::core::PeerIdentity owner{
    .principal = {0x4150505348454C4CULL, 0x0000000000000001ULL},
    .user = os::core::UserId{7U},
    .process = os::core::ProcessId{101U},
};

os::shell::ShellSnapshot active_snapshot() {
    os::shell::ShellSnapshot snapshot{};
    snapshot.revision = os::shell::ShellRevision{7U};
    snapshot.view = os::shell::ShellView::application;
    snapshot.active_instance = os::core::ApplicationInstanceId{11U};
    snapshot.task_count = 1U;
    snapshot.tasks[0] = os::shell::ShellTask{
        .instance = snapshot.active_instance,
        .application = application_identity(),
        .owner = owner,
        .root_surface = os::display::SurfaceId{
            os::display::make_display_object_value(9U, 3U)},
        .activation_serial = 1U,
    };
    return snapshot;
}

struct ActivationRecorder final {
    std::uint32_t calls {0U};
    os::core::PeerIdentity owner_seen {};
    os::display::SurfaceId surface_seen {};
    bool fail {false};
};

os::core::Result<void> activate_exact(
    void* context,
    os::core::PeerIdentity expected_owner,
    os::display::SurfaceId root_surface) noexcept {
    auto* recorder = static_cast<ActivationRecorder*>(context);
    if (recorder == nullptr) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::invalid_request);
    }
    ++recorder->calls;
    recorder->owner_seen = expected_owner;
    recorder->surface_seen = root_surface;
    if (recorder->fail) {
        return os::core::make_error(
            os::core::ErrorDomain::display,
            os::display::errors::activation_denied);
    }
    return {};
}

void expect_shell_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::shell);
    assert(error.code == code);
}

} // namespace

int main() {
    const auto current = active_snapshot();
    auto intent = os::shell::make_activation_intent(current);
    assert(intent);
    assert(intent.value().shell_revision == current.revision);
    assert(intent.value().instance == current.active_instance);
    assert(intent.value().owner == owner);
    assert(intent.value().root_surface == current.tasks[0].root_surface);

    ActivationRecorder recorder{};
    const os::shell::ExactActivationBackend backend{
        .context = &recorder,
        .activate = activate_exact,
    };
    auto committed = os::shell::commit_activation_intent(current, intent.value(), backend);
    assert(committed);
    assert(recorder.calls == 1U);
    assert(recorder.owner_seen == owner);
    assert(recorder.surface_seen == current.tasks[0].root_surface);

    // Any later shell mutation invalidates the old commit before the privileged
    // backend runs, even if the task fields happen to remain otherwise equal.
    auto newer = current;
    newer.revision = os::shell::ShellRevision{8U};
    auto stale_revision = os::shell::commit_activation_intent(newer, intent.value(), backend);
    assert(!stale_revision);
    expect_shell_error(stale_revision.error(), os::shell::errors::stale_activation_intent);
    assert(recorder.calls == 1U);

    // Exact process/root identity is part of the intent. A stale lifecycle join
    // cannot be redirected to a replacement process or surface.
    auto changed_owner = current;
    changed_owner.tasks[0].owner.process = os::core::ProcessId{202U};
    auto stale_owner = os::shell::commit_activation_intent(changed_owner, intent.value(), backend);
    assert(!stale_owner);
    expect_shell_error(stale_owner.error(), os::shell::errors::stale_activation_intent);
    assert(recorder.calls == 1U);

    // Home and overview are not implicit activation capabilities.
    auto home = current;
    home.view = os::shell::ShellView::home;
    home.active_instance = {};
    auto no_home_intent = os::shell::make_activation_intent(home);
    assert(!no_home_intent);
    expect_shell_error(no_home_intent.error(), os::shell::errors::invalid_activation_intent);

    // Backend failure is not hidden or converted into semantic success; the
    // caller can reconcile/retry rather than pretending the compositor commit
    // occurred.
    recorder.fail = true;
    auto failed_commit = os::shell::commit_activation_intent(current, intent.value(), backend);
    assert(!failed_commit);
    assert(failed_commit.error().domain == os::core::ErrorDomain::display);
    assert(failed_commit.error().code == os::display::errors::activation_denied);
    assert(recorder.calls == 2U);

    return 0;
}
