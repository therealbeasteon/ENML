#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <sys/types.h>

#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/package.hpp>
#include <os/package/persistence.hpp>
#include <os/sandbox/sandbox.hpp>
#include <os/supervisor/supervisor.hpp>

namespace os::app {

inline constexpr std::size_t m1_launch_target_capacity = 32U;
inline constexpr std::size_t m1_application_instance_capacity = 16U;

namespace manager_errors {
inline constexpr std::uint32_t invalid_target = 100U;
inline constexpr std::uint32_t target_conflict = 101U;
inline constexpr std::uint32_t target_capacity = 102U;
inline constexpr std::uint32_t target_not_found = 103U;
inline constexpr std::uint32_t principal_mismatch = 104U;
inline constexpr std::uint32_t executable_rejected = 105U;
inline constexpr std::uint32_t instance_capacity = 106U;
inline constexpr std::uint32_t unknown_instance = 107U;
inline constexpr std::uint32_t instance_id_exhausted = 108U;
} // namespace manager_errors

// This registration object is consumed only from a trusted Package Service
// path. Applications never provide it. The generation directory is already an
// authorized handle and entry_point is the normalized path emitted by the
// hostile package analyzer.
struct LaunchTargetRegistration final {
    os::package::PackageGenerationRecord package {};
    os::core::PrincipalId principal {};
    os::core::NativeHandle generation_directory {};
    os::package::ManifestPath entry_point {};
    os::sandbox::SandboxPolicyV1 sandbox {};
    std::uint32_t readiness_timeout_ms {1000U};
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

// M1.3 vertical-slice App Manager. Package identity and active generation are
// resolved from trusted durable package state. A launch request contains no
// Linux executable path and cannot choose its PrincipalId or sandbox policy.
class ApplicationManager final {
public:
    ApplicationManager(
        os::package::PersistentPackageRegistry& packages,
        os::supervisor::Supervisor& supervisor) noexcept;
    ~ApplicationManager();

    ApplicationManager(const ApplicationManager&) = delete;
    ApplicationManager& operator=(const ApplicationManager&) = delete;

    [[nodiscard]] os::core::Result<void>
    register_launch_target(LaunchTargetRegistration registration) noexcept;

    [[nodiscard]] os::core::Result<ApplicationInstanceInfo>
    launch(const os::package::PackageId& package_id, os::core::UserId user) noexcept;

    [[nodiscard]] os::core::Result<void> maintain() noexcept;

    [[nodiscard]] os::core::Result<ApplicationInstanceInfo>
    instance(os::core::ApplicationInstanceId instance_id) const noexcept;

    [[nodiscard]] os::core::Result<void>
    terminate(os::core::ApplicationInstanceId instance_id, int signal_number) noexcept;

private:
    struct LaunchTarget final {
        bool occupied {false};
        os::package::PackageGenerationRecord package {};
        os::core::PrincipalId principal {};
        os::core::NativeHandle executable {};
        os::sandbox::SandboxPolicyV1 sandbox {};
        std::uint32_t readiness_timeout_ms {1000U};
    };

    struct InstanceSlot final {
        bool occupied {false};
        ApplicationInstanceInfo info {};
    };

    os::package::PersistentPackageRegistry& packages_;
    os::supervisor::Supervisor& supervisor_;
    std::array<LaunchTarget, m1_launch_target_capacity> targets_ {};
    std::array<InstanceSlot, m1_application_instance_capacity> instances_ {};
    std::uint64_t next_instance_id_ {1U};

    [[nodiscard]] LaunchTarget*
    find_target(const os::package::PackageGenerationRecord& package) noexcept;
    [[nodiscard]] const LaunchTarget*
    find_target(const os::package::PackageGenerationRecord& package) const noexcept;
    [[nodiscard]] InstanceSlot* find_instance(os::core::ApplicationInstanceId instance_id) noexcept;
    [[nodiscard]] const InstanceSlot* find_instance(os::core::ApplicationInstanceId instance_id) const noexcept;
};

} // namespace os::app
