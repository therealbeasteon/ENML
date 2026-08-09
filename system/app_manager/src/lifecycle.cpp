#include <os/app/manager.hpp>

#include <cerrno>
#include <cstdint>

#include <signal.h>

#include <os/core/error.hpp>

namespace os::app {
namespace {

[[nodiscard]] constexpr os::core::Error manager_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] bool is_no_active_generation(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::package &&
        error.code == os::package::errors::no_active_generation;
}

[[nodiscard]] bool is_unknown_process(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::security &&
        error.code == os::core::errors::security::unknown_process;
}

} // namespace

ApplicationManager::LaunchTarget*
ApplicationManager::find_target(
    const os::package::ApplicationIdentity& application,
    os::package::PackageGenerationId generation) noexcept {
    for (auto& target : targets_) {
        if (target.occupied && target.package.application == application &&
            target.package.generation == generation) {
            return &target;
        }
    }
    return nullptr;
}

const ApplicationManager::LaunchTarget*
ApplicationManager::find_target(
    const os::package::ApplicationIdentity& application,
    os::package::PackageGenerationId generation) const noexcept {
    for (const auto& target : targets_) {
        if (target.occupied && target.package.application == application &&
            target.package.generation == generation) {
            return &target;
        }
    }
    return nullptr;
}

os::core::Result<std::uint32_t>
ApplicationManager::generation_pin_count(
    const os::package::ApplicationIdentity& application,
    os::package::PackageGenerationId generation) const noexcept {
    if (!application.valid() || generation.value() == 0U) {
        return manager_error(manager_errors::invalid_target);
    }
    if (find_target(application, generation) == nullptr) {
        return manager_error(manager_errors::target_not_found);
    }

    std::uint32_t count = 0U;
    for (const auto& slot : instances_) {
        if (!slot.occupied) continue;
        if (slot.info.application == application && slot.info.generation == generation) {
            ++count;
        }
    }
    return count;
}

os::core::Result<void>
ApplicationManager::retire_launch_target(
    const os::package::ApplicationIdentity& application,
    os::package::PackageGenerationId generation) noexcept {
    if (!application.valid() || generation.value() == 0U) {
        return manager_error(manager_errors::invalid_target);
    }

    // Reap any process that has already exited before making a retention
    // decision. App Manager is single-threaded in M1, so the instance table is
    // the authoritative live-generation pin set for this operation.
    auto refreshed = maintain();
    if (!refreshed) return refreshed.error();

    auto* target = find_target(application, generation);
    if (target == nullptr) return manager_error(manager_errors::target_not_found);

    auto pins = generation_pin_count(application, generation);
    if (!pins) return pins.error();
    if (pins.value() != 0U) {
        return manager_error(manager_errors::generation_in_use);
    }

    auto active = packages_.active(application);
    if (active) {
        if (active.value().generation == generation) {
            return manager_error(manager_errors::generation_active);
        }
    } else if (!is_no_active_generation(active.error())) {
        return active.error();
    }

    // Releasing the descriptor is the point after which Package Service may
    // physically remove this immutable generation's code directory. Historical
    // generation metadata stays in PackageRegistry as a small tombstone.
    *target = LaunchTarget{};
    return {};
}

os::core::Result<void>
ApplicationManager::uninstall_application(
    const os::package::ApplicationIdentity& application) noexcept {
    if (!application.valid()) {
        return manager_error(manager_errors::invalid_target);
    }

    // The durable no-active-generation state is committed first. From this
    // point forward, even a partial process-revocation failure cannot create a
    // new launch of the uninstalled package.
    auto persisted = packages_.uninstall(application);
    if (!persisted) return persisted.error();

    bool has_error = false;
    os::core::Error first_error {};

    for (auto& slot : instances_) {
        if (!slot.occupied || slot.info.application != application) continue;

        // Revoke supervisor-mediated service authority immediately rather than
        // waiting for SIGTERM delivery/process teardown. maintain() tolerates
        // the resulting unknown-process state when it later reaps the child.
        auto revoked = supervisor_.unregister_process(slot.info.identity.process);
        if (!revoked && !is_unknown_process(revoked.error()) && !has_error) {
            first_error = revoked.error();
            has_error = true;
        }

        if (::kill(slot.info.native_pid, SIGTERM) != 0 && errno != ESRCH && !has_error) {
            first_error = manager_error(os::core::errors::service::launch_failed);
            has_error = true;
        }
    }

    auto maintained = maintain();
    if (!maintained && !has_error) {
        first_error = maintained.error();
        has_error = true;
    }

    if (has_error) return first_error;
    return {};
}

} // namespace os::app
