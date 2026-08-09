#include <os/app/manager.hpp>

#include <array>
#include <cstddef>
#include <utility>

#include <os/ipc/constants.hpp>
#include <os/keys/error.hpp>

namespace os::app {
namespace {

[[nodiscard]] bool policy_not_registered(const os::core::Error& error) noexcept {
    return error == os::keys::key_error(os::keys::errors::policy_not_registered);
}

} // namespace

ApplicationManager::ApplicationManager(
    os::package::PersistentPackageRegistry& packages,
    ApplicationPrincipalStore& principals,
    os::supervisor::Supervisor& storage_supervisor,
    os::supervisor::Supervisor& key_supervisor) noexcept
    : packages_(packages),
      principals_(principals),
      supervisor_(storage_supervisor),
      key_supervisor_(&key_supervisor) {}

os::core::Result<void>
ApplicationManager::ensure_key_control() noexcept {
    if (key_supervisor_ == nullptr) return {};

    const auto status = key_supervisor_->status();
    if (status.state != os::supervisor::ServiceState::running || status.generation == 0U) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::not_running);
    }

    if (key_control_.valid() && key_control_client_.has_value() &&
        key_service_generation_ == status.generation) {
        return {};
    }

    // Policy is service-generation local. A restarted Key Service has a fresh
    // in-memory hierarchy/policy table even though durable KRG key state and
    // App Manager's desired lifecycle state survive independently.
    for (auto& profile : profiles_) {
        if (profile.occupied) profile.key_published = false;
    }

    key_control_client_.reset();
    key_control_.close();
    key_service_generation_ = 0U;

    auto control = key_supervisor_->connect_private_control();
    if (!control) return control.error();
    key_control_ = std::move(control).value();
    key_control_client_.emplace(key_control_);
    key_service_generation_ = status.generation;
    return {};
}

os::core::Result<void>
ApplicationManager::publish_key_profile(ApplicationProfile& profile) noexcept {
    if (!profile.occupied || !os::core::valid_principal(profile.principal)) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            manager_errors::invalid_profile);
    }
    if (key_supervisor_ == nullptr) return {};

    const bool was_enabled = profile.key_enabled;
    profile.key_enabled = true;

    auto control = ensure_key_control();
    if (!control) {
        profile.key_enabled = was_enabled;
        return control.error();
    }
    if (profile.key_published) return {};

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto profile_root = key_control_client_->ensure_profile(
        profile.user,
        scratch,
        1000U);
    if (!profile_root) {
        profile.key_enabled = was_enabled;
        return profile_root.error();
    }

    auto enabled = key_control_client_->enable_application(
        profile.principal,
        profile.user,
        scratch,
        1000U);
    if (!enabled) {
        profile.key_enabled = was_enabled;
        return enabled.error();
    }

    profile.key_published = true;
    return {};
}

os::core::Result<void>
ApplicationManager::revoke_key_profile(ApplicationProfile& profile) noexcept {
    if (!profile.occupied || !os::core::valid_principal(profile.principal)) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            manager_errors::invalid_profile);
    }

    profile.key_enabled = false;
    if (key_supervisor_ == nullptr) {
        profile.key_published = false;
        return {};
    }
    if (!profile.key_published) return {};

    // A dead/restarting service cannot retain live KeyObject endpoints. Treat
    // that generation as already revoked; a later generation will not receive
    // policy because key_enabled is now false.
    const auto status = key_supervisor_->status();
    if (status.state != os::supervisor::ServiceState::running || status.generation == 0U) {
        profile.key_published = false;
        key_control_client_.reset();
        key_control_.close();
        key_service_generation_ = 0U;
        return {};
    }

    auto control = ensure_key_control();
    if (!control) return control.error();
    // A generation transition clears publication state in ensure_key_control().
    if (!profile.key_published) return {};

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto revoked = key_control_client_->disable_application(
        profile.principal,
        profile.user,
        scratch,
        1000U);
    if (!revoked && !policy_not_registered(revoked.error())) return revoked.error();

    profile.key_published = false;
    return {};
}

os::core::Result<void>
ApplicationManager::republish_key_profiles_if_needed() noexcept {
    if (key_supervisor_ == nullptr) return {};

    const auto status = key_supervisor_->status();
    if (status.state != os::supervisor::ServiceState::running || status.generation == 0U) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::not_running);
    }
    if (key_control_.valid() && key_control_client_.has_value() &&
        key_service_generation_ == status.generation) {
        return {};
    }

    auto control = ensure_key_control();
    if (!control) return control.error();

    for (auto& profile : profiles_) {
        if (!profile.occupied || !profile.key_enabled) continue;
        auto published = publish_key_profile(profile);
        if (!published) {
            for (auto& reset_profile : profiles_) {
                if (reset_profile.occupied) reset_profile.key_published = false;
            }
            key_control_client_.reset();
            key_control_.close();
            key_service_generation_ = 0U;
            return published.error();
        }
    }
    return {};
}

} // namespace os::app
