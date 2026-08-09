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
encode_payload(
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

struct DecodedPayload final {
    os::core::ServiceId service {};
    std::uint64_t generation {0U};
};

[[nodiscard]] os::core::Result<DecodedPayload>
decode_payload(os::core::ByteSpan payload) noexcept {
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
    return DecodedPayload{
        .service = os::core::ServiceId{service.value()},
        .generation = generation.value(),
    };
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
    auto encoded = encode_payload(encoder, service, known_generation);
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

    auto decoded = decode_payload(response.payload());
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

os::core::Result<ServiceAcquireRequestV1>
receive_service_acquire_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept {
    auto received = channel.receive(receive_buffer);
    if (!received) return received.error();
    auto message = std::move(received).value();
    const auto& header = message.header();

    const bool valid_header =
        has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        !has_flag(header.flags, os::ipc::WireFlag::has_handles) &&
        header.service_id == application_service_session_id &&
        header.operation_id == application_service_session_operation_acquire &&
        header.request_id.value() != 0U &&
        header.handle_count == 0U && message.handle_count() == 0U;
    if (!valid_header) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    auto decoded = decode_payload(message.payload());
    if (!decoded) return decoded.error();
    return ServiceAcquireRequestV1{
        .request_header = header,
        .service = decoded.value().service,
        .known_generation = decoded.value().generation,
        .sender = message.sender_credentials(),
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
    auto encoded = encode_payload(encoder, service, generation);
    if (!encoded) return encoded.error();

    return os::ipc::send_rpc_response(
        channel,
        request_header,
        encoder.written(),
        std::span<const os::core::NativeHandle>{&endpoint, 1U});
}

} // namespace os::app
