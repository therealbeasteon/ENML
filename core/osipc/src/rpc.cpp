#include <os/ipc/rpc.hpp>

#include <array>
#include <limits>

#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/wire.hpp>

namespace os::ipc {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, WireFlag flag) noexcept {
    return (flags & flag_value(flag)) != 0U;
}

[[nodiscard]] constexpr bool valid_error_domain(std::uint16_t raw) noexcept {
    return raw >= static_cast<std::uint16_t>(os::core::ErrorDomain::core) &&
        raw <= static_cast<std::uint16_t>(os::core::ErrorDomain::key);
}

} // namespace

os::core::Result<InboundMessage>
ClientConnection::call(
    os::core::ServiceId service_id,
    std::uint32_t operation_id,
    os::core::ByteSpan request_payload,
    os::core::MutableByteSpan receive_buffer) noexcept {
    if (channel_ == nullptr || !channel_->valid()) {
        return ipc_error(errors::invalid_channel);
    }
    if (request_payload.size() > max_inline_payload_size) {
        return ipc_error(errors::oversized_message);
    }
    if (next_request_id_ == 0U) {
        return ipc_error(errors::request_id_exhausted);
    }

    const auto request_id = os::core::RequestId{next_request_id_};
    if (next_request_id_ == std::numeric_limits<std::uint64_t>::max()) {
        next_request_id_ = 0U;
    } else {
        ++next_request_id_;
    }

    const WireHeaderV1 request_header{
        .flags = flag_value(WireFlag::request),
        .service_id = service_id,
        .operation_id = operation_id,
        .request_id = request_id,
        .payload_size = static_cast<std::uint32_t>(request_payload.size()),
        .handle_count = 0,
        .payload_checksum = 0,
    };

    auto send_result = channel_->send(request_header, request_payload);
    if (!send_result) {
        return send_result.error();
    }

    auto receive_result = channel_->receive(receive_buffer);
    if (!receive_result) {
        return receive_result.error();
    }
    auto response = std::move(receive_result).value();
    const auto& header = response.header();

    if (!has_flag(header.flags, WireFlag::response) ||
        has_flag(header.flags, WireFlag::request) ||
        has_flag(header.flags, WireFlag::event) ||
        has_flag(header.flags, WireFlag::oneway) ||
        has_flag(header.flags, WireFlag::cancellable) ||
        header.service_id != service_id ||
        header.operation_id != operation_id ||
        header.request_id != request_id) {
        return ipc_error(errors::protocol_violation);
    }

    if (has_flag(header.flags, WireFlag::error)) {
        if (header.handle_count != 0U) {
            return ipc_error(errors::protocol_violation);
        }
        os::core::Error remote_error{};
        auto error_result = decode_rpc_error(response.payload(), remote_error);
        if (!error_result) {
            return error_result.error();
        }
        return remote_error;
    }

    return response;
}

os::core::Result<void>
send_rpc_response(
    Channel& channel,
    const WireHeaderV1& request_header,
    os::core::ByteSpan response_payload,
    std::span<const os::core::NativeHandle> handles) noexcept {
    if (response_payload.size() > max_inline_payload_size) {
        return ipc_error(errors::oversized_message);
    }
    if (handles.size() > max_handle_count ||
        handles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return ipc_error(errors::handle_count_mismatch);
    }
    for (const auto& handle : handles) {
        if (!handle.valid()) {
            return ipc_error(errors::invalid_native_handle);
        }
    }

    std::uint32_t flags = flag_value(WireFlag::response);
    if (!handles.empty()) {
        flags |= flag_value(WireFlag::has_handles);
    }

    const WireHeaderV1 response_header{
        .flags = flags,
        .service_id = request_header.service_id,
        .operation_id = request_header.operation_id,
        .request_id = request_header.request_id,
        .payload_size = static_cast<std::uint32_t>(response_payload.size()),
        .handle_count = static_cast<std::uint16_t>(handles.size()),
        .payload_checksum = 0,
    };
    return channel.send(response_header, response_payload, handles);
}

os::core::Result<void>
send_rpc_error(
    Channel& channel,
    const WireHeaderV1& request_header,
    os::core::Error error) noexcept {
    std::array<std::byte, 8> storage {};
    Encoder encoder{storage};
    auto result = encoder.write_u16_le(static_cast<std::uint16_t>(error.domain));
    if (!result) return result.error();
    result = encoder.write_u16_le(0U);
    if (!result) return result.error();
    result = encoder.write_u32_le(error.code);
    if (!result) return result.error();

    const WireHeaderV1 response_header{
        .flags = flag_value(WireFlag::response) | flag_value(WireFlag::error),
        .service_id = request_header.service_id,
        .operation_id = request_header.operation_id,
        .request_id = request_header.request_id,
        .payload_size = static_cast<std::uint32_t>(encoder.written().size()),
        .handle_count = 0,
        .payload_checksum = 0,
    };
    return channel.send(response_header, encoder.written());
}

os::core::Result<void>
decode_rpc_error(os::core::ByteSpan payload, os::core::Error& output) noexcept {
    Decoder decoder{payload};
    auto domain_result = decoder.read_u16_le();
    if (!domain_result) return domain_result.error();
    auto reserved_result = decoder.read_u16_le();
    if (!reserved_result) return reserved_result.error();
    auto code_result = decoder.read_u32_le();
    if (!code_result) return code_result.error();
    auto end_result = decoder.require_end();
    if (!end_result) return ipc_error(errors::protocol_violation);

    if (reserved_result.value() != 0U ||
        !valid_error_domain(domain_result.value()) ||
        code_result.value() == 0U) {
        return ipc_error(errors::protocol_violation);
    }

    output = os::core::Error{
        static_cast<os::core::ErrorDomain>(domain_result.value()),
        0U,
        code_result.value(),
    };
    return {};
}

os::core::Result<RequestContext>
validate_rpc_request(
    const InboundMessage& message,
    os::core::ServiceId expected_service,
    PeerIdentityResolver& identity_resolver) noexcept {
    const auto& header = message.header();
    if (!has_flag(header.flags, WireFlag::request) ||
        has_flag(header.flags, WireFlag::response) ||
        has_flag(header.flags, WireFlag::event) ||
        has_flag(header.flags, WireFlag::error) ||
        has_flag(header.flags, WireFlag::oneway) ||
        has_flag(header.flags, WireFlag::cancellable) ||
        header.service_id != expected_service ||
        header.request_id.value() == 0U ||
        header.handle_count != 0U) {
        return ipc_error(errors::protocol_violation);
    }

    auto identity_result = identity_resolver.resolve(message.sender_credentials());
    if (!identity_result) {
        return identity_result.error();
    }

    return RequestContext{
        .peer = identity_result.value(),
        .request_id = header.request_id,
    };
}

} // namespace os::ipc