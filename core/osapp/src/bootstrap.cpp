#include <os/app/bootstrap.hpp>

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

namespace os::app {
namespace {

inline constexpr os::core::RequestId bootstrap_request_id{1U};

[[nodiscard]] constexpr os::core::Error app_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] os::core::Result<void>
encode_record(os::ipc::Encoder& encoder, const ApplicationBootstrapRecordV1& record) noexcept {
    auto result = encoder.write_u16_le(application_bootstrap_version_v1);
    if (!result) return result.error();
    result = encoder.write_u16_le(application_bootstrap_payload_size_v1);
    if (!result) return result.error();
    result = encoder.write_u32_le(0U);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.instance.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.process.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.principal.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.principal.low);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.user.value());
    if (!result) return result.error();
    return encoder.write_u64_le(record.package_generation);
}

[[nodiscard]] os::core::Result<ApplicationBootstrapRecordV1>
decode_record(os::core::ByteSpan payload) noexcept {
    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    auto reserved = decoder.read_u32_le();
    if (!reserved) return reserved.error();
    auto instance = decoder.read_u64_le();
    if (!instance) return instance.error();
    auto process = decoder.read_u64_le();
    if (!process) return process.error();
    auto principal_high = decoder.read_u64_le();
    if (!principal_high) return principal_high.error();
    auto principal_low = decoder.read_u64_le();
    if (!principal_low) return principal_low.error();
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto generation = decoder.read_u64_le();
    if (!generation) return generation.error();
    auto end = decoder.require_end();
    if (!end) return end.error();

    const ApplicationBootstrapRecordV1 record{
        .instance = os::core::ApplicationInstanceId{instance.value()},
        .identity = os::core::PeerIdentity{
            .principal = os::core::PrincipalId{
                .high = principal_high.value(),
                .low = principal_low.value(),
            },
            .user = os::core::UserId{user.value()},
            .process = os::core::ProcessId{process.value()},
        },
        .package_generation = generation.value(),
    };
    if (version.value() != application_bootstrap_version_v1 ||
        size.value() != application_bootstrap_payload_size_v1 || reserved.value() != 0U ||
        record.instance.value() == 0U || !os::core::valid_peer_identity(record.identity) ||
        record.package_generation == 0U) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }
    return record;
}

} // namespace

os::core::Result<void>
send_bootstrap_request(
    os::ipc::Channel& channel,
    const ApplicationBootstrapRecordV1& record) noexcept {
    std::array<std::byte, application_bootstrap_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encode = encode_record(encoder, record);
    if (!encode) return encode.error();

    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = application_bootstrap_service_id,
        .operation_id = application_bootstrap_operation_initialize,
        .request_id = bootstrap_request_id,
        .payload_size = static_cast<std::uint32_t>(encoder.written().size()),
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    return channel.send(header, encoder.written());
}

os::core::Result<ApplicationBootstrapRequest>
receive_bootstrap_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept {
    auto receive = channel.receive(receive_buffer);
    if (!receive) return receive.error();
    auto message = std::move(receive).value();
    const auto& header = message.header();

    const bool valid_header = has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        header.service_id == application_bootstrap_service_id &&
        header.operation_id == application_bootstrap_operation_initialize &&
        header.request_id == bootstrap_request_id && header.handle_count == 0U &&
        message.handle_count() == 0U;
    if (!valid_header) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }

    auto record = decode_record(message.payload());
    if (!record) return record.error();
    return ApplicationBootstrapRequest{
        .request_header = header,
        .record = record.value(),
    };
}

os::core::Result<void>
send_ready(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    const ApplicationBootstrapRecordV1& record) noexcept {
    std::array<std::byte, application_bootstrap_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encode = encode_record(encoder, record);
    if (!encode) return encode.error();
    return os::ipc::send_rpc_response(channel, request_header, encoder.written());
}

os::core::Result<void>
wait_for_ready(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
    const ApplicationBootstrapRecordV1& expected,
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
        return app_error(os::core::errors::service::readiness_timeout);
    }
    if (poll_result < 0) {
        return app_error(os::core::errors::service::launch_failed);
    }

    auto receive = channel.receive(receive_buffer);
    if (!receive) return receive.error();
    auto message = std::move(receive).value();
    const auto& header = message.header();
    const bool valid_header = has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        header.service_id == application_bootstrap_service_id &&
        header.operation_id == application_bootstrap_operation_initialize &&
        header.request_id == bootstrap_request_id && header.handle_count == 0U &&
        message.handle_count() == 0U;
    if (!valid_header) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }

    auto record = decode_record(message.payload());
    if (!record || record.value() != expected) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }
    return {};
}

} // namespace os::app
