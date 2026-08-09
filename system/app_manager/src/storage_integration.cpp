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

    // A new Storage process generation owns a fresh in-memory root/object
    // registry. Nothing published to the dead generation remains published,
    // even though App Manager still retains the durable profile policy.
    for (auto& profile : profiles_) {
        if (profile.occupied) profile.storage_published = false;
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

    // Key policy is installed first. If Storage publication then fails while
    // activating a previously-disabled profile, revoke the newly-added Key
    // policy so profile registration/reinstall does not leave half-authorized
    // lifecycle state. This is the no-exceptions equivalent of preserving a
    // construction invariant across multiple owned resources.
    const bool key_was_enabled = profile.key_enabled;
    auto key_policy = publish_key_profile(profile);
    if (!key_policy) return key_policy.error();

    const bool storage_was_enabled = profile.storage_enabled;
    profile.storage_enabled = true;
    auto control = ensure_storage_control();
    if (!control) {
        profile.storage_enabled = storage_was_enabled;
        if (!key_was_enabled) {
            auto rollback = revoke_key_profile(profile);
            if (!rollback) return rollback.error();
        }
        return control.error();
    }
    if (profile.storage_published) return {};

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto published = storage_control_client_->register_private_root(
        profile.principal,
        profile.user,
        profile.private_data_directory,
        scratch,
        1000U);
    if (!published) {
        profile.storage_enabled = storage_was_enabled;
        if (!key_was_enabled) {
            auto rollback = revoke_key_profile(profile);
            if (!rollback) return rollback.error();
        }
        return published.error();
    }
    profile.storage_published = true;
    return {};
}

os::core::Result<void>
ApplicationManager::revoke_profile(ApplicationProfile& profile) noexcept {
    if (!profile.occupied || !os::core::valid_principal(profile.principal) ||
        !profile.private_data_directory.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            manager_errors::invalid_profile);
    }

    // Key authority is revoked before Storage/process teardown. Durable keys are
    // deliberately retained; revoke_key_profile() only removes lifecycle
    // admission and closes already-minted KeyObject endpoints.
    bool has_error = false;
    os::core::Error first_error{};
    auto key_revoked = revoke_key_profile(profile);
    if (!key_revoked) {
        first_error = key_revoked.error();
        has_error = true;
    }

    profile.storage_enabled = false;
    if (profile.storage_published) {
        auto control = ensure_storage_control();
        if (!control) {
            if (!has_error) {
                first_error = control.error();
                has_error = true;
            }
        } else if (profile.storage_published) {
            // ensure_storage_control() may have observed a service restart and
            // cleared publication state. In that case the dead generation
            // already destroyed every old object endpoint.
            std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
            auto revoked = storage_control_client_->unregister_private_root(
                profile.principal,
                profile.user,
                scratch,
                1000U);
            if (!revoked) {
                if (!has_error) {
                    first_error = revoked.error();
                    has_error = true;
                }
            } else {
                profile.storage_published = false;
            }
        }
    }

    if (has_error) return first_error;
    return {};
}

os::core::Result<void>
ApplicationManager::republish_profiles_if_needed() noexcept {
    const auto status = supervisor_.status();
    if (status.state != os::supervisor::ServiceState::running || status.generation == 0U) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::not_running);
    }

    const bool storage_current = storage_control_.valid() &&
        storage_control_client_.has_value() &&
        storage_service_generation_ == status.generation;
    if (!storage_current) {
        auto control = ensure_storage_control();
        if (!control) return control.error();

        // Reconcile Storage independently of Key Service availability. This
        // avoids coupling a recovered filesystem service to a separate crypto
        // service generation while preserving the same durable profile owner.
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        for (auto& profile : profiles_) {
            if (!profile.occupied || !profile.storage_enabled || profile.storage_published) continue;
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
                for (auto& reset_profile : profiles_) {
                    if (reset_profile.occupied) reset_profile.storage_published = false;
                }
                return published.error();
            }
            profile.storage_published = true;
        }
    }

    // Key hierarchy/admission policy is also generation-local, but durable key
    // metadata is not. Replay only desired `key_enabled` profiles; profiles
    // revoked by uninstall remain absent after a service restart.
    auto keys = republish_key_profiles_if_needed();
    if (!keys) return keys.error();
    return {};
}

} // namespace os::app
