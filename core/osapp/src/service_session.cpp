#include <os/app/service_session.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::app {
namespace {

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
encode_service_payload(
    os::ipc::Encoder& encoder,
    os::core::ServiceId service,
    std::uint64_t generation) noexcept {
    if (service.value() == 0U) {
        return service_error(os::core::errors::service::invalid_request);
    }
    auto result = encoder.write_u16_le(application_service_session_version_v1);
    if (!result) return result.error();
    result = encoder.write_u16_le(application_service_session_payload_size_v1);
    if (!result) return result.error();
    result = encoder.write_u32_le(service.value());
    if (!result) return result.error();
    return encoder.write_u64_le(generation);
}

[[nodiscard]] os::core::Result<void>
encode_empty_capability_payload(
    os::ipc::Encoder& encoder,
    std::uint16_t payload_size) noexcept {
    auto result = encoder.write_u16_le(application_service_session_version_v1);
    if (!result) return result.error();
    return encoder.write_u16_le(payload_size);
}

[[nodiscard]] os::core::Result<void>
encode_session_endpoint_payload(
    os::ipc::Encoder& encoder,
    std::uint16_t payload_size,
    std::uint64_t session_id) noexcept {
    if (session_id == 0U) {
        return service_error(os::core::errors::service::invalid_request);
    }
    auto result = encode_empty_capability_payload(encoder, payload_size);
    if (!result) return result.error();
    return encoder.write_u64_le(session_id);
}

struct DecodedServicePayload final {
    os::core::ServiceId service {};
    std::uint64_t generation {0U};
};

[[nodiscard]] os::core::Result<DecodedServicePayload>
decode_service_payload(os::core::ByteSpan payload) noexcept {
    if (payload.size() != application_service_session_payload_size_v1) {
        return service_error(os::core::errors::service::invalid_request);
    }

    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    auto service = decoder.read_u32_le();
    if (!service) return service.error();
    auto generation = decoder.read_u64_le();
    if (!generation) return generation.error();
    auto end = decoder.require_end();
    if (!end) return service_error(os::core::errors::service::invalid_request);

    if (version.value() != application_service_session_version_v1 ||
        size.value() != application_service_session_payload_size_v1 ||
        service.value() == 0U) {
        return service_error(os::core::errors::service::invalid_request);
    }
    return DecodedServicePayload{
        .service = os::core::ServiceId{service.value()},
        .generation = generation.value(),
    };
}

[[nodiscard]] os::core::Result<void>
decode_empty_capability_payload(
    os::core::ByteSpan payload,
    std::uint16_t expected_size) noexcept {
    if (payload.size() != expected_size) {
        return service_error(os::core::errors::service::invalid_request);
    }
    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    auto end = decoder.require_end();
    if (!end || version.value() != application_service_session_version_v1 ||
        size.value() != expected_size) {
        return service_error(os::core::errors::service::invalid_request);
    }
    return {};
}

[[nodiscard]] os::core::Result<std::uint64_t>
decode_session_endpoint_payload(
    os::core::ByteSpan payload,
    std::uint16_t expected_size) noexcept {
    if (payload.size() != expected_size) {
        return service_error(os::core::errors::service::invalid_request);
    }
    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto end = decoder.require_end();
    if (!end || version.value() != application_service_session_version_v1 ||
        size.value() != expected_size || session.value() == 0U) {
        return service_error(os::core::errors::service::invalid_request);
    }
    return session.value();
}

[[nodiscard]] bool runtime_request_header_valid(const os::ipc::InboundMessage& message) noexcept {
    const auto& header = message.header();
    return has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        !has_flag(header.flags, os::ipc::WireFlag::has_handles) &&
        header.service_id == application_service_session_id &&
        header.request_id.value() != 0U &&
        header.handle_count == 0U && message.handle_count() == 0U &&
        (header.operation_id == application_service_session_operation_acquire ||
         header.operation_id == application_service_session_operation_acquire_input_events ||
         header.operation_id == application_service_session_operation_acquire_accessibility ||
         header.operation_id == application_service_session_operation_acquire_collection);
}

[[nodiscard]] os::core::Result<os::ipc::Channel>
take_single_channel_response(
    os::ipc::InboundMessage&& response,
    std::uint32_t expected_operation) noexcept {
    const auto& header = response.header();
    if (header.service_id != application_service_session_id ||
        header.operation_id != expected_operation ||
        !has_flag(header.flags, os::ipc::WireFlag::response) ||
        !has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
        header.handle_count != 1U || response.handle_count() != 1U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    auto handle = response.take_handle(0U);
    if (!handle) return handle.error();
    auto channel = os::ipc::Channel::adopt(std::move(handle).value());
    if (!channel) return channel.error();
    return std::move(channel).value();
}

} // namespace

os::core::Result<PlatformServiceEndpoint>
PlatformServiceSession::acquire(
    os::core::ServiceId service,
    std::uint64_t known_generation,
    os::core::MutableByteSpan receive_buffer) noexcept {
    if (service.value() == 0U) {
        return service_error(os::core::errors::service::invalid_request);
    }

    std::array<std::byte, application_service_session_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_service_payload(encoder, service, known_generation);
    if (!encoded) return encoded.error();

    auto call = connection_.call(
        application_service_session_id,
        application_service_session_operation_acquire,
        encoder.written(),
        receive_buffer);
    if (!call) return call.error();

    auto response = std::move(call).value();
    const auto& header = response.header();
    if (!has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
        header.handle_count != 1U || response.handle_count() != 1U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    auto decoded = decode_service_payload(response.payload());
    if (!decoded) return decoded.error();
    if (decoded.value().service != service || decoded.value().generation == 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    auto handle = response.take_handle(0U);
    if (!handle) return handle.error();
    auto channel = os::ipc::Channel::adopt(std::move(handle).value());
    if (!channel) return channel.error();

    PlatformServiceEndpoint endpoint{};
    endpoint.service = decoded.value().service;
    endpoint.generation = decoded.value().generation;
    endpoint.channel = std::move(channel).value();
    if (!endpoint.valid()) return ipc_error(os::ipc::errors::protocol_violation);
    return endpoint;
}

os::core::Result<os::ipc::Channel>
PlatformServiceSession::acquire_input_events(
    os::core::MutableByteSpan receive_buffer) noexcept {
    std::array<std::byte, application_input_endpoint_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_empty_capability_payload(
        encoder,
        application_input_endpoint_payload_size_v1);
    if (!encoded) return encoded.error();

    auto call = connection_.call(
        application_service_session_id,
        application_service_session_operation_acquire_input_events,
        encoder.written(),
        receive_buffer);
    if (!call) return call.error();
    auto response = std::move(call).value();
    auto payload_valid = decode_empty_capability_payload(
        response.payload(),
        application_input_endpoint_payload_size_v1);
    if (!payload_valid) return payload_valid.error();
    return take_single_channel_response(
        std::move(response),
        application_service_session_operation_acquire_input_events);
}

os::core::Result<PlatformAccessibilityEndpoint>
PlatformServiceSession::acquire_accessibility(
    os::core::MutableByteSpan receive_buffer) noexcept {
    std::array<std::byte, application_input_endpoint_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_empty_capability_payload(
        encoder,
        application_input_endpoint_payload_size_v1);
    if (!encoded) return encoded.error();

    auto call = connection_.call(
        application_service_session_id,
        application_service_session_operation_acquire_accessibility,
        encoder.written(),
        receive_buffer);
    if (!call) return call.error();
    auto response = std::move(call).value();
    auto session = decode_session_endpoint_payload(
        response.payload(),
        application_accessibility_endpoint_payload_size_v1);
    if (!session) return session.error();
    auto channel = take_single_channel_response(
        std::move(response),
        application_service_session_operation_acquire_accessibility);
    if (!channel) return channel.error();

    PlatformAccessibilityEndpoint endpoint{};
    endpoint.session_id = session.value();
    endpoint.channel = std::move(channel).value();
    if (!endpoint.valid()) return ipc_error(os::ipc::errors::protocol_violation);
    return endpoint;
}

os::core::Result<PlatformCollectionEndpoint>
PlatformServiceSession::acquire_collection(
    os::core::MutableByteSpan receive_buffer) noexcept {
    std::array<std::byte, application_input_endpoint_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_empty_capability_payload(
        encoder,
        application_input_endpoint_payload_size_v1);
    if (!encoded) return encoded.error();

    auto call = connection_.call(
        application_service_session_id,
        application_service_session_operation_acquire_collection,
        encoder.written(),
        receive_buffer);
    if (!call) return call.error();
    auto response = std::move(call).value();
    auto session = decode_session_endpoint_payload(
        response.payload(),
        application_collection_endpoint_payload_size_v1);
    if (!session) return session.error();
    auto channel = take_single_channel_response(
        std::move(response),
        application_service_session_operation_acquire_collection);
    if (!channel) return channel.error();

    PlatformCollectionEndpoint endpoint{};
    endpoint.session_id = session.value();
    endpoint.channel = std::move(channel).value();
    if (!endpoint.valid()) return ipc_error(os::ipc::errors::protocol_violation);
    return endpoint;
}

os::core::Result<RuntimeSessionRequestV1>
receive_runtime_session_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept {
    auto received = channel.receive(receive_buffer);
    if (!received) return received.error();
    auto message = std::move(received).value();
    if (!runtime_request_header_valid(message)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    const auto& header = message.header();
    if (header.operation_id == application_service_session_operation_acquire) {
        auto decoded = decode_service_payload(message.payload());
        if (!decoded) return decoded.error();
        return RuntimeSessionRequestV1{
            .request_header = header,
            .kind = RuntimeSessionRequestKind::acquire_service,
            .service = decoded.value().service,
            .known_generation = decoded.value().generation,
            .sender = message.sender_credentials(),
        };
    }

    auto decoded = decode_empty_capability_payload(
        message.payload(),
        application_input_endpoint_payload_size_v1);
    if (!decoded) return decoded.error();

    RuntimeSessionRequestKind kind = RuntimeSessionRequestKind::acquire_accessibility;
    if (header.operation_id == application_service_session_operation_acquire_input_events) {
        kind = RuntimeSessionRequestKind::acquire_input_events;
    } else if (header.operation_id == application_service_session_operation_acquire_collection) {
        kind = RuntimeSessionRequestKind::acquire_collection;
    }
    return RuntimeSessionRequestV1{
        .request_header = header,
        .kind = kind,
        .service = {},
        .known_generation = 0U,
        .sender = message.sender_credentials(),
    };
}

os::core::Result<ServiceAcquireRequestV1>
receive_service_acquire_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept {
    auto request = receive_runtime_session_request(channel, receive_buffer);
    if (!request) return request.error();
    if (request.value().kind != RuntimeSessionRequestKind::acquire_service) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return ServiceAcquireRequestV1{
        .request_header = request.value().request_header,
        .service = request.value().service,
        .known_generation = request.value().known_generation,
        .sender = request.value().sender,
    };
}

os::core::Result<void>
send_service_acquire_response(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    os::core::ServiceId service,
    std::uint64_t generation,
    const os::core::NativeHandle& endpoint) noexcept {
    if (request_header.service_id != application_service_session_id ||
        request_header.operation_id != application_service_session_operation_acquire ||
        request_header.request_id.value() == 0U ||
        service.value() == 0U || generation == 0U || !endpoint.valid()) {
        return service_error(os::core::errors::service::invalid_request);
    }

    std::array<std::byte, application_service_session_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_service_payload(encoder, service, generation);
    if (!encoded) return encoded.error();

    return os::ipc::send_rpc_response(
        channel,
        request_header,
        encoder.written(),
        std::span<const os::core::NativeHandle>{&endpoint, 1U});
}

os::core::Result<void>
send_input_event_endpoint_response(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    const os::core::NativeHandle& endpoint) noexcept {
    if (request_header.service_id != application_service_session_id ||
        request_header.operation_id != application_service_session_operation_acquire_input_events ||
        request_header.request_id.value() == 0U || !endpoint.valid()) {
        return service_error(os::core::errors::service::invalid_request);
    }
    std::array<std::byte, application_input_endpoint_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_empty_capability_payload(
        encoder,
        application_input_endpoint_payload_size_v1);
    if (!encoded) return encoded.error();
    return os::ipc::send_rpc_response(
        channel,
        request_header,
        encoder.written(),
        std::span<const os::core::NativeHandle>{&endpoint, 1U});
}

os::core::Result<void>
send_accessibility_endpoint_response(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    std::uint64_t session_id,
    const os::core::NativeHandle& endpoint) noexcept {
    if (request_header.service_id != application_service_session_id ||
        request_header.operation_id != application_service_session_operation_acquire_accessibility ||
        request_header.request_id.value() == 0U || session_id == 0U || !endpoint.valid()) {
        return service_error(os::core::errors::service::invalid_request);
    }
    std::array<std::byte, application_accessibility_endpoint_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_session_endpoint_payload(
        encoder,
        application_accessibility_endpoint_payload_size_v1,
        session_id);
    if (!encoded) return encoded.error();
    return os::ipc::send_rpc_response(
        channel,
        request_header,
        encoder.written(),
        std::span<const os::core::NativeHandle>{&endpoint, 1U});
}

os::core::Result<void>
send_collection_endpoint_response(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    std::uint64_t session_id,
    const os::core::NativeHandle& endpoint) noexcept {
    if (request_header.service_id != application_service_session_id ||
        request_header.operation_id != application_service_session_operation_acquire_collection ||
        request_header.request_id.value() == 0U || session_id == 0U || !endpoint.valid()) {
        return service_error(os::core::errors::service::invalid_request);
    }
    std::array<std::byte, application_collection_endpoint_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_session_endpoint_payload(
        encoder,
        application_collection_endpoint_payload_size_v1,
        session_id);
    if (!encoded) return encoded.error();
    return os::ipc::send_rpc_response(
        channel,
        request_header,
        encoder.written(),
        std::span<const os::core::NativeHandle>{&endpoint, 1U});
}

} // namespace os::app
