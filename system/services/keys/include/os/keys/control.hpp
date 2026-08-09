#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/keys/hierarchy.hpp>
#include <os/keys/policy.hpp>
#include <os/keys/service.hpp>
#include <os/service/identity.hpp>

namespace os::keys {

// Private bootstrap-control operations. Applications never receive the service
// control channel and therefore cannot publish their own key hierarchy policy.
inline constexpr std::uint32_t key_control_ensure_profile_operation = 120U;
inline constexpr std::uint32_t key_control_enable_application_operation = 121U;
inline constexpr std::uint32_t key_control_disable_application_operation = 122U;

// ENML-owned profile-principal namespace used only to name the trusted per-user
// profile root. It is derived by system code from UserId and is not a Linux uid.
inline constexpr std::uint64_t key_profile_principal_namespace = 0x50524F46454E4D4CULL;

[[nodiscard]] constexpr KeyProtectionBinding
profile_key_binding(os::core::UserId user) noexcept {
    return KeyProtectionBinding{
        .scope = KeyProtectionScope::user_profile,
        .owner = KeyOwner{
            .principal = os::core::PrincipalId{
                key_profile_principal_namespace,
                user.value(),
            },
            .user = user,
        },
    };
}

[[nodiscard]] constexpr KeyProtectionBinding
application_key_binding(os::core::PrincipalId principal, os::core::UserId user) noexcept {
    return KeyProtectionBinding{
        .scope = KeyProtectionScope::application,
        .owner = KeyOwner{.principal = principal, .user = user},
    };
}

class KeyControlClient final {
public:
    explicit KeyControlClient(os::ipc::Channel& control) noexcept : control_(&control) {}

    [[nodiscard]] os::core::Result<void>
    ensure_profile(
        os::core::UserId user,
        os::core::MutableByteSpan scratch,
        std::uint32_t timeout_ms) noexcept;

    [[nodiscard]] os::core::Result<void>
    enable_application(
        os::core::PrincipalId principal,
        os::core::UserId user,
        os::core::MutableByteSpan scratch,
        std::uint32_t timeout_ms) noexcept;

    [[nodiscard]] os::core::Result<void>
    disable_application(
        os::core::PrincipalId principal,
        os::core::UserId user,
        os::core::MutableByteSpan scratch,
        std::uint32_t timeout_ms) noexcept;

private:
    [[nodiscard]] os::core::Result<void>
    send_request(
        std::uint32_t operation,
        os::core::ByteSpan payload,
        os::core::MutableByteSpan scratch,
        std::uint32_t timeout_ms) noexcept;

    os::ipc::Channel* control_ {nullptr};
    std::uint64_t next_request_id_ {0x8100000000000001ULL};
};

// Routes the service's private bootstrap channel. It preserves supervisor
// process-identity registration and adds hierarchy/policy operations. Possession
// of this control channel is system authority; the public Key endpoint never
// accepts caller-selected KeyOwner or KeyProtectionScope.
class KeyControlRouter final {
public:
    KeyControlRouter(
        ApplicationKeyPolicy& policy,
        KeyHierarchy& hierarchy,
        KeyService& service,
        os::service::IdentityRegistry& identities) noexcept
        : policy_(&policy),
          hierarchy_(&hierarchy),
          service_(&service),
          identities_(&identities) {}

    [[nodiscard]] os::core::Result<void>
    dispatch_once(
        os::ipc::Channel& control,
        os::core::MutableByteSpan receive_buffer) noexcept;

private:
    ApplicationKeyPolicy* policy_ {nullptr};
    KeyHierarchy* hierarchy_ {nullptr};
    KeyService* service_ {nullptr};
    os::service::IdentityRegistry* identities_ {nullptr};
};

} // namespace os::keys
