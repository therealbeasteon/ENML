#include <os/app/manager.hpp>

#include <array>
#include <cstddef>
#include <utility>

#include <os/ipc/constants.hpp>

namespace os::app {

os::core::Result<void>
ApplicationManager::ensure_storage_control() noexcept {
    const auto status = supervisor_.status();
    if (status.state != os::supervisor::ServiceState::running || status.generation == 0U) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::not_running);
    }

    if (storage_control_.valid() && storage_control_client_.has_value() &&
        storage_service_generation_ == status.generation) {
        return {};
    }

    storage_control_client_.reset();
    storage_control_.close();
    storage_service_generation_ = 0U;

    auto control = supervisor_.connect_private_control();
    if (!control) return control.error();
    storage_control_ = std::move(control).value();
    storage_control_client_.emplace(storage_control_);
    storage_service_generation_ = status.generation;
    return {};
}

os::core::Result<void>
ApplicationManager::publish_profile(ApplicationProfile& profile) noexcept {
    if (!profile.occupied || !os::core::valid_principal(profile.principal) ||
        !profile.private_data_directory.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            manager_errors::invalid_profile);
    }

    auto control = ensure_storage_control();
    if (!control) return control.error();

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    return storage_control_client_->register_private_root(
        profile.principal,
        profile.user,
        profile.private_data_directory,
        scratch,
        1000U);
}

os::core::Result<void>
ApplicationManager::republish_profiles_if_needed() noexcept {
    const auto status = supervisor_.status();
    if (status.state != os::supervisor::ServiceState::running || status.generation == 0U) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::not_running);
    }
    if (storage_control_.valid() && storage_control_client_.has_value() &&
        storage_service_generation_ == status.generation) {
        return {};
    }

    auto control = ensure_storage_control();
    if (!control) return control.error();

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    for (auto& profile : profiles_) {
        if (!profile.occupied) continue;
        auto published = storage_control_client_->register_private_root(
            profile.principal,
            profile.user,
            profile.private_data_directory,
            scratch,
            1000U);
        if (!published) {
            storage_control_client_.reset();
            storage_control_.close();
            storage_service_generation_ = 0U;
            return published.error();
        }
    }
    return {};
}

} // namespace os::app
