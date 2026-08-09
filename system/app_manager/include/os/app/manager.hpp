#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <sys/types.h>

#include <os/app/principal_store.hpp>
#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/package.hpp>
#include <os/package/persistence.hpp>
#include <os/sandbox/sandbox.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/supervisor.hpp>

namespace os::app {

inline constexpr std::size_t m1_launch_target_capacity = 32U;
inline constexpr std::size_t m1_application_profile_capacity = 64U;
inline constexpr std::size_t m1_application_instance_capacity = 16U;

namespace manager_errors {
inline constexpr std::uint32_t invalid_target = 100U;
inline constexpr std::uint32_t target_conflict = 101U;
inline constexpr std::uint32_t target_capacity = 102U;
inline constexpr std::uint32_t target_not_found = 103U;
inline constexpr std::uint32_t profile_conflict = 104U;
inline constexpr std::uint32_t executable_rejected = 105U;
inline constexpr std::uint32_t instance_capacity = 106U;
inline constexpr std::uint32_t unknown_instance = 107U;
inline constexpr std::uint32_t instance_id_exhausted = 108U;
inline constexpr std::uint32_t invalid_profile = 109U;
inline constexpr std::uint32_t profile_capacity = 110U;
inline constexpr std::uint32_t profile_not_found = 111U;
inline constexpr std::uint32_t generation_in_use = 112U;
inline constexpr std::uint32_t generation_active = 113U;
} // namespace manager_errors

// Trusted Package Service registration. Applications never supply executable
// paths. The generation directory is already authorized and entry_point is the
// normalized package-analyzer path. App Manager opens and retains the exact
// executable object at registration time.
struct LaunchTargetRegistration final {
    os::package::PackageGenerationRecord package {};
    os::core::NativeHandle generation_directory {};
    os::package::ManifestPath entry_point {};
    std::uint32_t readiness_timeout_ms {1000U};
};

// Trusted per-user application policy. The private data directory is an
// already-authorized handle. PrincipalId is deliberately absent: App Manager
// resolves/allocates it from ApplicationPrincipalStore and publishes the root
// to Storage Service over its system-only control channel.
struct ApplicationProfileRegistration final {
    os::package::ApplicationIdentity application {};
    os::core::UserId user {};
    os::core::NativeHandle private_data_directory {};
    os::sandbox::SandboxPolicyV1 sandbox {};
};

struct ApplicationInstanceInfo final {
    os::core::ApplicationInstanceId instance {};
    os::package::ApplicationIdentity application {};
    os::package::PackageGenerationId generation {};
    os::package::ContentDigest content {};
    os::core::PeerIdentity identity {};
    pid_t native_pid {-1};

    [[nodiscard]] bool valid() const noexcept {
        return instance.value() != 0U && application.valid() && generation.value() != 0U &&
            content.valid() && os::core::valid_peer_identity(identity) && native_pid > 0;
    }
};

// M2.2 App Manager. Launch requests contain only PackageId plus trusted user
// context. Active generation, executable object, per-user principal, native
// credentials, sandbox policy and Storage root policy all come from trusted
// state. Applications receive a Storage Service connection rather than their
// private-data Linux directory descriptor.
class ApplicationManager final {
public:
    ApplicationManager(
        os::package::PersistentPackageRegistry& packages,
        ApplicationPrincipalStore& principals,
        os::supervisor::Supervisor& supervisor) noexcept;
    ~ApplicationManager();

    ApplicationManager(const ApplicationManager&) = delete;
    ApplicationManager& operator=(const ApplicationManager&) = delete;

    [[nodiscard]] os::core::Result<void>
    register_launch_target(LaunchTargetRegistration registration) noexcept;

    [[nodiscard]] os::core::Result<void>
    register_application_profile(ApplicationProfileRegistration registration) noexcept;

    [[nodiscard]] os::core::Result<ApplicationInstanceInfo>
    launch(const os::package::PackageId& package_id, os::core::UserId user) noexcept;

    [[nodiscard]] os::core::Result<void>
    uninstall_application(const os::package::ApplicationIdentity& application) noexcept;

    [[nodiscard]] os::core::Result<void>
    retire_launch_target(
        const os::package::ApplicationIdentity& application,
        os::package::PackageGenerationId generation) noexcept;

    [[nodiscard]] os::core::Result<std::uint32_t>
    generation_pin_count(
        const os::package::ApplicationIdentity& application,
        os::package::PackageGenerationId generation) const noexcept;

    [[nodiscard]] os::core::Result<void> maintain() noexcept;

    [[nodiscard]] os::core::Result<ApplicationInstanceInfo>
    instance(os::core::ApplicationInstanceId instance_id) const noexcept;

    [[nodiscard]] os::core::Result<void>
    terminate(os::core::ApplicationInstanceId instance_id, int signal_number) noexcept;

private:
    struct LaunchTarget final {
        bool occupied {false};
        os::package::PackageGenerationRecord package {};
        os::core::NativeHandle executable {};
        std::uint32_t readiness_timeout_ms {1000U};
    };

    struct ApplicationProfile final {
        bool occupied {false};
        os::package::ApplicationIdentity application {};
        os::core::UserId user {};
        os::core::PrincipalId principal {};
        os::core::NativeHandle private_data_directory {};
        os::sandbox::SandboxPolicyV1 sandbox {};
    };

    struct InstanceSlot final {
        bool occupied {false};
        ApplicationInstanceInfo info {};
    };

    os::package::PersistentPackageRegistry& packages_;
    ApplicationPrincipalStore& principals_;
    os::supervisor::Supervisor& supervisor_;
    std::array<LaunchTarget, m1_launch_target_capacity> targets_ {};
    std::array<ApplicationProfile, m1_application_profile_capacity> profiles_ {};
    std::array<InstanceSlot, m1_application_instance_capacity> instances_ {};
    std::uint64_t next_instance_id_ {1U};

    os::ipc::Channel storage_control_ {};
    std::optional<os::storage::StorageControlClient> storage_control_client_ {};
    std::uint64_t storage_service_generation_ {0U};

    [[nodiscard]] os::core::Result<void> ensure_storage_control() noexcept;
    [[nodiscard]] os::core::Result<void> publish_profile(ApplicationProfile& profile) noexcept;
    [[nodiscard]] os::core::Result<void> republish_profiles_if_needed() noexcept;

    [[nodiscard]] LaunchTarget*
    find_target(const os::package::PackageGenerationRecord& package) noexcept;
    [[nodiscard]] const LaunchTarget*
    find_target(const os::package::PackageGenerationRecord& package) const noexcept;
    [[nodiscard]] LaunchTarget*
    find_target(
        const os::package::ApplicationIdentity& application,
        os::package::PackageGenerationId generation) noexcept;
    [[nodiscard]] const LaunchTarget*
    find_target(
        const os::package::ApplicationIdentity& application,
        os::package::PackageGenerationId generation) const noexcept;
    [[nodiscard]] ApplicationProfile*
    find_profile(const os::package::ApplicationIdentity& application, os::core::UserId user) noexcept;
    [[nodiscard]] const ApplicationProfile*
    find_profile(const os::package::ApplicationIdentity& application, os::core::UserId user) const noexcept;
    [[nodiscard]] InstanceSlot* find_instance(os::core::ApplicationInstanceId instance_id) noexcept;
    [[nodiscard]] const InstanceSlot* find_instance(os::core::ApplicationInstanceId instance_id) const noexcept;
};

} // namespace os::app
