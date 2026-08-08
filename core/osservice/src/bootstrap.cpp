#include <os/service/bootstrap.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>

namespace os::service {
namespace {

inline constexpr std::size_t bootstrap_payload_size = 44U;
inline constexpr os::core::RequestId bootstrap_request_id{1U};

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] os::core::Result<void>
encode_record(os::ipc::Encoder& encoder, const BootstrapRecordV1& record) noexcept {
    auto result = encoder.write_u64_le(record.identity.process.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.principal.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.principal.low);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.user.value());
    if (!result) return result.error();
    result = encoder.write_u32_le(record.service_id.value());
    if (!result) return result.error();
    return encoder.write_u64_le(record.boot_generation);
}

[[nodiscard]] os::core::Result<BootstrapRecordV1>
decode_record(os::core::ByteSpan payload) noexcept {
    os::ipc::Decoder decoder{payload};
    auto process_result = decoder.read_u64_le();
    if (!process_result) return process_result.error();
    auto principal_high_result = decoder.read_u64_le();
    if (!principal_high_result) return principal_high_result.error();
    auto principal_low_result = decoder.read_u64_le();
    if (!principal_low_result) return principal_low_result.error();
    auto user_result = decoder.read_u64_le();
    if (!user_result) return user_result.error();
    auto service_result = decoder.read_u32_le();
    if (!service_result) return service_result.error();
    auto boot_result = decoder.read_u64_le();
    if (!boot_result) return boot_result.error();
    auto end_result = decoder.require_end();
    if (!end_result) return end_result.error();

    const os::core::PeerIdentity identity{
        .principal = os::core::PrincipalId{
            .high = principal_high_result.value(),
            .low = principal_low_result.value(),
        },
        .user = os::core::UserId{user_result.value()},
        .process = os::core::ProcessId{process_result.value()},
    };
    if (!os::core::valid_peer_identity(identity) ||
        service_result.value() == 0U || boot_result.value() == 0U) {
        return service_error(os::core::errors::service::invalid_bootstrap);
    }

    return BootstrapRecordV1{
        .identity = identity,
        .service_id = os::core::ServiceId{service_result.value()},
        .boot_generation = boot_result.value(),
    };
}

} // namespace

os::core::Result<void>
send_bootstrap_request(os::ipc::Channel& channel, const BootstrapRecordV1& record) noexcept {
    std::array<std::byte, bootstrap_payload_size> payload{};
    os::ipc::Encoder encoder{payload};
    auto encode_result = encode_record(encoder, record);
    if (!encode_result) {
        return encode_result.error();
    }

    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = bootstrap_service_id,
        .operation_id = bootstrap_operation_initialize,
        .request_id = bootstrap_request_id,
        .payload_size = static_cast<std::uint32_t>(encoder.written().size()),
        .handle_count = 0,
        .payload_checksum = 0,
    };
    return channel.send(header, encoder.written());
}

os::core::Result<void>
wait_for_ready(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
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

    auto receive_result = channel.receive(receive_buffer);
    if (!receive_result) {
        return receive_result.error();
    }
    auto message = std::move(receive_result).value();
    const auto& header = message.header();

    const bool valid = has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        header.service_id == bootstrap_service_id &&
        header.operation_id == bootstrap_operation_initialize &&
        header.request_id == bootstrap_request_id &&
        header.payload_size == 0U &&
        message.payload().empty() &&
        header.handle_count == 0U;
    if (!valid) {
        return service_error(os::core::errors::service::invalid_bootstrap);
    }
    return {};
}

os::core::Result<BootstrapRequest>
receive_bootstrap_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
    os::core::ServiceId expected_service) noexcept {
    auto receive_result = channel.receive(receive_buffer);
    if (!receive_result) {
        return receive_result.error();
    }
    auto message = std::move(receive_result).value();
    const auto& header = message.header();

    const bool valid_header = has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        header.service_id == bootstrap_service_id &&
        header.operation_id == bootstrap_operation_initialize &&
        header.request_id == bootstrap_request_id &&
        header.handle_count == 0U &&
        message.handle_count() == 0U;
    if (!valid_header) {
        return service_error(os::core::errors::service::invalid_bootstrap);
    }

    auto record_result = decode_record(message.payload());
    if (!record_result) {
        return record_result.error();
    }
    if (record_result.value().service_id != expected_service) {
        return service_error(os::core::errors::service::invalid_bootstrap);
    }

    return BootstrapRequest{
        .request_header = header,
        .record = record_result.value(),
    };
}

os::core::Result<void>
send_ready(os::ipc::Channel& channel, const os::ipc::WireHeaderV1& request_header) noexcept {
    return os::ipc::send_rpc_response(channel, request_header, {});
}

} // namespace os::service
