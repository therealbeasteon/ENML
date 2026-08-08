#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::service {

inline constexpr std::uint32_t identity_operation_register = 2U;
inline constexpr std::uint32_t identity_operation_unregister = 3U;
inline constexpr std::size_t max_identity_registry_entries = 64U;

struct ProcessIdentityRecord final {
    os::ipc::KernelPeerCredentials kernel {};
    os::core::PeerIdentity peer {};
};

class IdentityRegistry final : public os::ipc::PeerIdentityResolver {
public:
    IdentityRegistry() noexcept = default;

    IdentityRegistry(const IdentityRegistry&) = delete;
    IdentityRegistry& operator=(const IdentityRegistry&) = delete;

    [[nodiscard]] os::core::Result<void>
    register_process(ProcessIdentityRecord record, os::core::NativeHandle pidfd) noexcept;

    [[nodiscard]] os::core::Result<void>
    unregister_process(os::core::ProcessId process_id) noexcept;

    [[nodiscard]] os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry final {
        bool occupied {false};
        ProcessIdentityRecord record {};
        os::core::NativeHandle pidfd {};
    };

    std::array<Entry, max_identity_registry_entries> entries_ {};

    [[nodiscard]] static bool pidfd_alive(int fd) noexcept;
};

// Supervisor-side request. The pidfd is duplicated by SCM_RIGHTS; ownership
// remains with the caller as well.
[[nodiscard]] os::core::Result<void>
register_identity_with_service(
    os::ipc::Channel& control,
    const ProcessIdentityRecord& record,
    const os::core::NativeHandle& pidfd,
    os::core::RequestId request_id,
    std::uint32_t timeout_ms) noexcept;

[[nodiscard]] os::core::Result<void>
unregister_identity_with_service(
    os::ipc::Channel& control,
    os::core::ProcessId process_id,
    os::core::RequestId request_id,
    std::uint32_t timeout_ms) noexcept;

// Service-side control-plane handler. It receives exactly one control request,
// updates the registry, and returns an ACK or stable error response.
[[nodiscard]] os::core::Result<void>
handle_identity_control_once(
    os::ipc::Channel& control,
    os::core::MutableByteSpan receive_buffer,
    IdentityRegistry& registry) noexcept;

} // namespace os::service
