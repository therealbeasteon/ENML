#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <sys/types.h>

#include <os/accessibility/transport.hpp>
#include <os/app/bootstrap.hpp>
#include <os/app/input_event.hpp>
#include <os/app/principal_store.hpp>
#include <os/app/service_session.hpp>
#include <os/collection/session.hpp>
#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/keys/control.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/package.hpp>
#include <os/package/persistence.hpp>
#include <os/sandbox/sandbox.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/service_broker.hpp>
#include <os/supervisor/supervisor.hpp>

namespace os::app {

inline constexpr std::size_t m1_launch_target_capacity = 32U;
inline constexpr std::size_t m1_application_profile_capacity = 64U;
inline constexpr std::size_t m1_application_instance_capacity = 16U;
inline constexpr std::size_t max_collection_sessions_per_instance = 8U;

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
inline constexpr std::uint32_t broker_misconfigured = 114U;
inline constexpr std::uint32_t input_target_not_found = 115U;
inline constexpr std::uint32_t input_endpoint_unavailable = 116U;
inline constexpr std::uint32_t input_event_replay = 117U;
inline constexpr std::uint32_t accessibility_target_not_found = 118U;
inline constexpr std::uint32_t accessibility_endpoint_unavailable = 119U;
inline constexpr std::uint32_t accessibility_authority_denied = 120U;
inline constexpr std::uint32_t accessibility_session_exhausted = 121U;
inline constexpr std::uint32_t collection_session_capacity = 122U;
inline constexpr std::uint32_t collection_session_exhausted = 123U;
inline constexpr std::uint32_t collection_authority_denied = 124U;
inline constexpr std::uint32_t collection_endpoint_unavailable = 125U;
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
// resolves/allocates it from ApplicationPrincipalStore and publishes Storage
// and Key Service policy from the resulting durable PrincipalId + UserId.
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

// Move-only service side of one private accessibility capability. App Manager
// releases it only to the trusted accessibility principal and only for the
// exact live application PeerIdentity that requested the paired endpoint.
struct BrokeredAccessibilityEndpoint final {
    std::uint64_t session_id {0U};
    os::core::PeerIdentity application {};
    os::ipc::Channel channel {};

    BrokeredAccessibilityEndpoint() noexcept = default;
    BrokeredAccessibilityEndpoint(const BrokeredAccessibilityEndpoint&) = delete;
    BrokeredAccessibilityEndpoint& operator=(const BrokeredAccessibilityEndpoint&) = delete;
    BrokeredAccessibilityEndpoint(BrokeredAccessibilityEndpoint&&) noexcept = default;
    BrokeredAccessibilityEndpoint& operator=(BrokeredAccessibilityEndpoint&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return session_id != 0U && os::core::valid_peer_identity(application) && channel.valid();
    }
};

// Move-only consumer side of one private application-produced collection
// session. Session authority is the conjunction of exact live application
// identity, runtime-minted session id and this channel; the numeric id alone is
// not a globally usable object reference.
struct BrokeredCollectionEndpoint final {
    std::uint64_t session_id {0U};
    os::core::PeerIdentity application {};
    os::ipc::Channel channel {};

    BrokeredCollectionEndpoint() noexcept = default;
    BrokeredCollectionEndpoint(const BrokeredCollectionEndpoint&) = delete;
    BrokeredCollectionEndpoint& operator=(const BrokeredCollectionEndpoint&) = delete;
    BrokeredCollectionEndpoint(BrokeredCollectionEndpoint&&) noexcept = default;
    BrokeredCollectionEndpoint& operator=(BrokeredCollectionEndpoint&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return session_id != 0U && os::core::valid_peer_identity(application) && channel.valid();
    }
};

// M2 App Manager. Launch requests contain only PackageId plus trusted user
// context. Active generation, executable object, per-user principal, native
// credentials, sandbox policy and service policy all come from trusted state.
//
// Three-argument mode preserves the Storage-only M1/M2.2 launch contract.
// Four-argument mode adds the M2.8 Key lifecycle policy publisher but keeps the
// legacy single fixed Storage endpoint bootstrap.
// Five-argument M2.9+ mode requires Storage + Keys to share one ProcessAuthority
// with a trusted ServiceBroker. Bootstrap v2 transfers initial service channels
// by ServiceId. M2.10 keeps that same bootstrap channel open after READY as a
// bounded runtime platform-service session for generation-bound reacquisition.
class ApplicationManager final {
public:
    ApplicationManager(
        os::package::PersistentPackageRegistry& packages,
        ApplicationPrincipalStore& principals,
        os::supervisor::Supervisor& supervisor) noexcept;

    ApplicationManager(
        os::package::PersistentPackageRegistry& packages,
        ApplicationPrincipalStore& principals,
        os::supervisor::Supervisor& storage_supervisor,
        os::supervisor::Supervisor& key_supervisor) noexcept;

    ApplicationManager(
        os::package::PersistentPackageRegistry& packages,
        ApplicationPrincipalStore& principals,
        os::supervisor::Supervisor& storage_supervisor,
        os::supervisor::Supervisor& key_supervisor,
        os::supervisor::ServiceBroker& service_broker) noexcept;

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

    // Trusted-runtime delivery seam. The event already names the compositor-
    // issued exact owner. App Manager finds that live instance by exact
    // PeerIdentity; callers cannot redirect delivery with an ApplicationInstanceId.
    [[nodiscard]] os::core::Result<void>
    deliver_input_event(const ApplicationInputEventV1& event) noexcept;

    // One-shot trusted accessibility-service claim. `caller` must already be a
    // supervisor/runtime-resolved identity for the canonical accessibility
    // principal; `target` must exactly match one live application identity.
    // Knowledge of a numeric session id or application ProcessId is not enough
    // to obtain the capability.
    [[nodiscard]] os::core::Result<BrokeredAccessibilityEndpoint>
    take_accessibility_endpoint(
        os::core::PeerIdentity caller,
        os::core::PeerIdentity target) noexcept;

    // One-shot collection consumer claim. Unlike accessibility, applications
    // may host several collections concurrently, so the trusted consumer must
    // name the exact runtime-minted session in addition to the exact live app.
    // The caller identity must already come from trusted runtime/supervisor
    // resolution and own collection_consumer_principal.
    [[nodiscard]] os::core::Result<BrokeredCollectionEndpoint>
    take_collection_endpoint(
        os::core::PeerIdentity caller,
        os::core::PeerIdentity target,
        std::uint64_t session_id) noexcept;

private:
    struct LaunchTarget final {
        bool occupied {false};
        os::package::PackageGenerationRecord package {};
        os::core::NativeHandle executable {};
        std::uint32_t readiness_timeout_ms {1000U};
    };

    struct ApplicationProfile final {
        bool occupied {false};
        bool storage_enabled {false};
        bool storage_published {false};
        bool key_enabled {false};
        bool key_published {false};
        os::package::ApplicationIdentity application {};
        os::core::UserId user {};
        os::core::PrincipalId principal {};
        os::core::NativeHandle private_data_directory {};
        os::sandbox::SandboxPolicyV1 sandbox {};
    };

    struct CollectionEndpointSlot final {
        std::uint64_t session_id {0U};
        os::ipc::Channel consumer_endpoint {};
    };

    struct InstanceSlot final {
        bool occupied {false};
        ApplicationInstanceInfo info {};
        os::ipc::Channel service_session {};
        os::ipc::Channel input_event_sender {};
        std::uint64_t last_input_event_sequence {0U};
        os::ipc::Channel accessibility_service_endpoint {};
        std::uint64_t accessibility_session_id {0U};
        std::array<CollectionEndpointSlot, max_collection_sessions_per_instance>
            collection_endpoints {};
        std::array<os::core::ServiceId, max_application_service_endpoints_v2> services {};
        std::uint16_t service_count {0U};
    };

    os::package::PersistentPackageRegistry& packages_;
    ApplicationPrincipalStore& principals_;
    os::supervisor::Supervisor& supervisor_;
    os::supervisor::Supervisor* key_supervisor_ {nullptr};
    os::supervisor::ServiceBroker* service_broker_ {nullptr};
    std::array<LaunchTarget, m1_launch_target_capacity> targets_ {};
    std::array<ApplicationProfile, m1_application_profile_capacity> profiles_ {};
    std::array<InstanceSlot, m1_application_instance_capacity> instances_ {};
    std::uint64_t next_instance_id_ {1U};
    std::uint64_t next_accessibility_session_id_ {1U};
    std::uint64_t next_collection_session_id_ {1U};

    os::ipc::Channel storage_control_ {};
    std::optional<os::storage::StorageControlClient> storage_control_client_ {};
    std::uint64_t storage_service_generation_ {0U};

    os::ipc::Channel key_control_ {};
    std::optional<os::keys::KeyControlClient> key_control_client_ {};
    std::uint64_t key_service_generation_ {0U};

    [[nodiscard]] bool broker_configuration_valid() const noexcept;
    [[nodiscard]] os::core::Result<void> release_instance_identity(
        os::core::ProcessId process) noexcept;

    [[nodiscard]] bool service_allowed(
        const InstanceSlot& slot,
        os::core::ServiceId service) const noexcept;
    [[nodiscard]] os::core::Result<void>
    service_runtime_session_once(InstanceSlot& slot) noexcept;
    [[nodiscard]] os::core::Result<void>
    service_runtime_session_if_pending(InstanceSlot& slot) noexcept;

    [[nodiscard]] os::core::Result<void> ensure_storage_control() noexcept;
    [[nodiscard]] os::core::Result<void> publish_profile(ApplicationProfile& profile) noexcept;
    [[nodiscard]] os::core::Result<void> revoke_profile(ApplicationProfile& profile) noexcept;
    [[nodiscard]] os::core::Result<void> republish_profiles_if_needed() noexcept;

    [[nodiscard]] os::core::Result<void> ensure_key_control() noexcept;
    [[nodiscard]] os::core::Result<void> publish_key_profile(ApplicationProfile& profile) noexcept;
    [[nodiscard]] os::core::Result<void> revoke_key_profile(ApplicationProfile& profile) noexcept;
    [[nodiscard]] os::core::Result<void> republish_key_profiles_if_needed() noexcept;

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
