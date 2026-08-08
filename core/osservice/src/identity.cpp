#include <os/service/identity.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/wire.hpp>
#include <os/service/bootstrap.hpp>

namespace os::service {
namespace {

inline constexpr std::size_t identity_payload_size = 48U;
inline constexpr std::size_t unregister_payload_size = 8U;

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] os::core::Result<void>
encode_identity_record(os::ipc::Encoder& encoder, const ProcessIdentityRecord& record) noexcept {
    auto result = encoder.write_u64_le(record.peer.process.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(record.peer.principal.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.peer.principal.low);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.peer.user.value());
    if (!result) return result.error();
    if (record.kernel.process_id <= 0) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    result = encoder.write_u64_le(static_cast<std::uint64_t>(record.kernel.process_id));
    if (!result) return result.error();
    result = encoder.write_u32_le(record.kernel.user_id);
    if (!result) return result.error();
    return encoder.write_u32_le(record.kernel.group_id);
}

[[nodiscard]] os::core::Result<ProcessIdentityRecord>
decode_identity_record(os::core::ByteSpan payload) noexcept {
    os::ipc::Decoder decoder{payload};
    auto process_result = decoder.read_u64_le();
    if (!process_result) return process_result.error();
    auto principal_high_result = decoder.read_u64_le();
    if (!principal_high_result) return principal_high_result.error();
    auto principal_low_result = decoder.read_u64_le();
    if (!principal_low_result) return principal_low_result.error();
    auto user_result = decoder.read_u64_le();
    if (!user_result) return user_result.error();
    auto native_pid_result = decoder.read_u64_le();
    if (!native_pid_result) return native_pid_result.error();
    auto native_uid_result = decoder.read_u32_le();
    if (!native_uid_result) return native_uid_result.error();
    auto native_gid_result = decoder.read_u32_le();
    if (!native_gid_result) return native_gid_result.error();
    auto end_result = decoder.require_end();
    if (!end_result) return end_result.error();

    if (process_result.value() == 0U ||
        native_pid_result.value() == 0U ||
        native_pid_result.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return security_error(os::core::errors::security::invalid_identity);
    }

    const ProcessIdentityRecord record{
        .kernel = os::ipc::KernelPeerCredentials{
            .process_id = static_cast<std::int64_t>(native_pid_result.value()),
            .user_id = native_uid_result.value(),
            .group_id = native_gid_result.value(),
        },
        .peer = os::core::PeerIdentity{
            .principal = os::core::PrincipalId{
                .high = principal_high_result.value(),
                .low = principal_low_result.value(),
            },
            .user = os::core::UserId{user_result.value()},
            .process = os::core::ProcessId{process_result.value()},
        },
    };
    if (!os::core::valid_peer_identity(record.peer)) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    return record;
}

[[nodiscard]] os::core::Result<void>
wait_for_control_response(
    os::ipc::Channel& channel,
    os::core::RequestId request_id,
    std::uint32_t operation_id,
    std::uint32_t timeout_ms) noexcept {
    pollfd descriptor{
        .fd = channel.native_fd(),
        .events = POLLIN,
        .revents = 0,
    };
    int poll_result = -1;
    do {
        poll_result = ::poll(&descriptor, 1, static_cast<int>(timeout_ms));
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result == 0) {
        return service_error(os::core::errors::service::readiness_timeout);
    }
    if (poll_result < 0) {
        return service_error(os::core::errors::service::launch_failed);
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> response_buffer{};
    auto receive_result = channel.receive(response_buffer);
    if (!receive_result) return receive_result.error();
    auto response = std::move(receive_result).value();
    const auto& header = response.header();
    if (!has_flag(header.flags, os::ipc::WireFlag::response) ||
        has_flag(header.flags, os::ipc::WireFlag::request) ||
        has_flag(header.flags, os::ipc::WireFlag::event) ||
        header.service_id != bootstrap_service_id ||
        header.operation_id != operation_id ||
        header.request_id != request_id ||
        header.handle_count != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    if (has_flag(header.flags, os::ipc::WireFlag::error)) {
        os::core::Error remote{};
        auto decode_result = os::ipc::decode_rpc_error(response.payload(), remote);
        if (!decode_result) return decode_result.error();
        return remote;
    }
    if (!response.payload().empty()) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

} // namespace

bool IdentityRegistry::pidfd_alive(int fd) noexcept {
    if (fd < 0) return false;
    pollfd descriptor{
        .fd = fd,
        .events = POLLIN,
        .revents = 0,
    };
    int result = -1;
    do {
        result = ::poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return false;
    return result == 0;
}

os::core::Result<void>
IdentityRegistry::register_process(ProcessIdentityRecord record, os::core::NativeHandle pidfd) noexcept {
    if (!pidfd.valid() || !os::core::valid_peer_identity(record.peer) || record.kernel.process_id <= 0) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    if (!pidfd_alive(pidfd.native())) {
        return security_error(os::core::errors::security::stale_process);
    }

    Entry* free_entry = nullptr;
    for (auto& entry : entries_) {
        if (!entry.occupied) {
            if (free_entry == nullptr) free_entry = &entry;
            continue;
        }
        if (entry.record.peer.process == record.peer.process ||
            entry.record.kernel.process_id == record.kernel.process_id) {
            if (entry.record.peer == record.peer && entry.record.kernel == record.kernel &&
                pidfd_alive(entry.pidfd.native())) {
                return {};
            }
            return security_error(os::core::errors::security::credential_mismatch);
        }
    }
    if (free_entry == nullptr) {
        return security_error(os::core::errors::security::registry_full);
    }

    free_entry->occupied = true;
    free_entry->record = record;
    free_entry->pidfd = std::move(pidfd);
    return {};
}

os::core::Result<void>
IdentityRegistry::unregister_process(os::core::ProcessId process_id) noexcept {
    if (process_id.value() == 0U) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    for (auto& entry : entries_) {
        if (entry.occupied && entry.record.peer.process == process_id) {
            entry.pidfd.reset();
            entry = Entry{};
            return {};
        }
    }
    return security_error(os::core::errors::security::unknown_process);
}

os::core::Result<os::core::PeerIdentity>
IdentityRegistry::resolve(os::ipc::KernelPeerCredentials credentials) noexcept {
    if (credentials.process_id <= 0) {
        return security_error(os::core::errors::security::unknown_process);
    }

    for (auto& entry : entries_) {
        if (!entry.occupied || entry.record.kernel.process_id != credentials.process_id) {
            continue;
        }
        if (entry.record.kernel.user_id != credentials.user_id ||
            entry.record.kernel.group_id != credentials.group_id) {
            return security_error(os::core::errors::security::credential_mismatch);
        }
        if (!pidfd_alive(entry.pidfd.native())) {
            entry.pidfd.reset();
            entry = Entry{};
            return security_error(os::core::errors::security::stale_process);
        }
        return entry.record.peer;
    }
    return security_error(os::core::errors::security::unknown_process);
}

std::size_t IdentityRegistry::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& entry : entries_) {
        if (entry.occupied) ++count;
    }
    return count;
}

os::core::Result<void>
register_identity_with_service(
    os::ipc::Channel& control,
    const ProcessIdentityRecord& record,
    const os::core::NativeHandle& pidfd,
    os::core::RequestId request_id,
    std::uint32_t timeout_ms) noexcept {
    if (!pidfd.valid() || request_id.value() == 0U) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    std::array<std::byte, identity_payload_size> storage{};
    os::ipc::Encoder encoder{storage};
    auto encode_result = encode_identity_record(encoder, record);
    if (!encode_result) return encode_result.error();

    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request) |
            os::ipc::flag_value(os::ipc::WireFlag::has_handles),
        .service_id = bootstrap_service_id,
        .operation_id = identity_operation_register,
        .request_id = request_id,
        .payload_size = static_cast<std::uint32_t>(encoder.written().size()),
        .handle_count = 1U,
        .payload_checksum = 0U,
    };
    const std::span<const os::core::NativeHandle> handles{&pidfd, 1U};
    auto send_result = control.send(header, encoder.written(), handles);
    if (!send_result) return send_result.error();
    return wait_for_control_response(control, request_id, identity_operation_register, timeout_ms);
}

os::core::Result<void>
unregister_identity_with_service(
    os::ipc::Channel& control,
    os::core::ProcessId process_id,
    os::core::RequestId request_id,
    std::uint32_t timeout_ms) noexcept {
    if (process_id.value() == 0U || request_id.value() == 0U) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    std::array<std::byte, unregister_payload_size> storage{};
    os::ipc::Encoder encoder{storage};
    auto encode_result = encoder.write_u64_le(process_id.value());
    if (!encode_result) return encode_result.error();
    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = bootstrap_service_id,
        .operation_id = identity_operation_unregister,
        .request_id = request_id,
        .payload_size = static_cast<std::uint32_t>(encoder.written().size()),
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    auto send_result = control.send(header, encoder.written());
    if (!send_result) return send_result.error();
    return wait_for_control_response(control, request_id, identity_operation_unregister, timeout_ms);
}

os::core::Result<void>
handle_identity_control_once(
    os::ipc::Channel& control,
    os::core::MutableByteSpan receive_buffer,
    IdentityRegistry& registry) noexcept {
    auto receive_result = control.receive(receive_buffer);
    if (!receive_result) return receive_result.error();
    auto message = std::move(receive_result).value();
    const auto& header = message.header();

    const bool base_valid = has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        header.service_id == bootstrap_service_id &&
        header.request_id.value() != 0U;
    if (!base_valid) {
        return service_error(os::core::errors::service::invalid_bootstrap);
    }

    os::core::Result<void> operation_result{};
    if (header.operation_id == identity_operation_register) {
        if (!has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
            header.handle_count != 1U || message.handle_count() != 1U) {
            operation_result = security_error(os::core::errors::security::invalid_identity);
        } else {
            auto record_result = decode_identity_record(message.payload());
            if (!record_result) {
                operation_result = record_result.error();
            } else {
                auto pidfd_result = message.take_handle(0U);
                if (!pidfd_result) {
                    operation_result = pidfd_result.error();
                } else {
                    operation_result = registry.register_process(
                        record_result.value(), std::move(pidfd_result).value());
                }
            }
        }
    } else if (header.operation_id == identity_operation_unregister) {
        if (header.handle_count != 0U || message.handle_count() != 0U ||
            has_flag(header.flags, os::ipc::WireFlag::has_handles)) {
            operation_result = security_error(os::core::errors::security::invalid_identity);
        } else {
            os::ipc::Decoder decoder{message.payload()};
            auto process_result = decoder.read_u64_le();
            if (!process_result) {
                operation_result = process_result.error();
            } else {
                auto end_result = decoder.require_end();
                if (!end_result) {
                    operation_result = end_result.error();
                } else {
                    operation_result = registry.unregister_process(
                        os::core::ProcessId{process_result.value()});
                }
            }
        }
    } else {
        operation_result = service_error(os::core::errors::service::unknown_operation);
    }

    if (!operation_result) {
        return os::ipc::send_rpc_error(control, header, operation_result.error());
    }
    return os::ipc::send_rpc_response(control, header, {});
}

} // namespace os::service
