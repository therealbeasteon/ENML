#include <os/app/accessibility_control.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <os/accessibility/transport.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::app {
namespace {

[[nodiscard]] constexpr os::core::Error manager_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error protocol_error() noexcept {
    return os::core::make_error(
        os::core::ErrorDomain::ipc,
        os::ipc::errors::protocol_violation);
}

[[nodiscard]] os::core::Result<void> encode_identity(
    os::ipc::Encoder& encoder,
    os::core::PeerIdentity identity) noexcept {
    if (!os::core::valid_peer_identity(identity)) return protocol_error();
    auto result = encoder.write_u64_le(identity.principal.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(identity.principal.low);
    if (!result) return result.error();
    result = encoder.write_u64_le(identity.user.value());
    if (!result) return result.error();
    return encoder.write_u64_le(identity.process.value());
}

[[nodiscard]] os::core::Result<os::core::PeerIdentity> decode_identity(
    os::ipc::Decoder& decoder) noexcept {
    auto high = decoder.read_u64_le();
    if (!high) return high.error();
    auto low = decoder.read_u64_le();
    if (!low) return low.error();
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto process = decoder.read_u64_le();
    if (!process) return process.error();

    const os::core::PeerIdentity identity{
        .principal = {high.value(), low.value()},
        .user = os::core::UserId{user.value()},
        .process = os::core::ProcessId{process.value()},
    };
    if (!os::core::valid_peer_identity(identity)) return protocol_error();
    return identity;
}

[[nodiscard]] os::core::Result<std::size_t> encode_claim_request(
    os::core::PeerIdentity target,
    os::core::MutableByteSpan output) noexcept {
    if (output.size() < accessibility_broker_claim_request_size_v1) {
        return protocol_error();
    }
    os::ipc::Encoder encoder{output};
    auto result = encode_identity(encoder, target);
    if (!result) return result.error();
    if (encoder.written().size() != accessibility_broker_claim_request_size_v1) {
        return protocol_error();
    }
    return encoder.written().size();
}

[[nodiscard]] os::core::Result<os::core::PeerIdentity> decode_claim_request(
    os::core::ByteSpan payload) noexcept {
    if (payload.size() != accessibility_broker_claim_request_size_v1) {
        return protocol_error();
    }
    os::ipc::Decoder decoder{payload};
    auto target = decode_identity(decoder);
    if (!target) return target.error();
    auto end = decoder.require_end();
    if (!end) return protocol_error();
    return target.value();
}

[[nodiscard]] os::core::Result<std::size_t> encode_claim_response(
    const BrokeredAccessibilityEndpoint& endpoint,
    os::core::MutableByteSpan output) noexcept {
    if (!endpoint.valid() || output.size() < accessibility_broker_claim_response_size_v1) {
        return protocol_error();
    }
    os::ipc::Encoder encoder{output};
    auto result = encoder.write_u64_le(endpoint.session_id);
    if (!result) return result.error();
    result = encode_identity(encoder, endpoint.application);
    if (!result) return result.error();
    if (encoder.written().size() != accessibility_broker_claim_response_size_v1) {
        return protocol_error();
    }
    return encoder.written().size();
}

[[nodiscard]] os::core::Result<std::pair<std::uint64_t, os::core::PeerIdentity>>
decode_claim_response(os::core::ByteSpan payload) noexcept {
    if (payload.size() != accessibility_broker_claim_response_size_v1) {
        return protocol_error();
    }
    os::ipc::Decoder decoder{payload};
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto application = decode_identity(decoder);
    if (!application) return application.error();
    auto end = decoder.require_end();
    if (!end || session.value() == 0U) return protocol_error();
    return std::pair{session.value(), application.value()};
}

[[nodiscard]] os::core::Result<BrokeredAccessibilityEndpoint> manager_claim(
    void* context,
    os::core::PeerIdentity caller,
    os::core::PeerIdentity target) noexcept {
    if (context == nullptr) {
        return manager_error(manager_errors::accessibility_endpoint_unavailable);
    }
    return static_cast<ApplicationManager*>(context)->take_accessibility_endpoint(caller, target);
}

} // namespace

AccessibilityEndpointBrokerBackend accessibility_endpoint_backend(
    ApplicationManager& manager) noexcept {
    return AccessibilityEndpointBrokerBackend{
        .context = &manager,
        .claim = manager_claim,
    };
}

os::core::Result<void> AccessibilityBrokerControlServer::dispatch_once(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid() || !channel.valid()) {
        return manager_error(os::core::errors::service::invalid_request);
    }

    auto received = channel.receive(scratch);
    if (!received) return received.error();
    auto message = std::move(received).value();
    const auto request_header = message.header();

    auto context = os::ipc::validate_rpc_request(
        message,
        accessibility_broker_control_service_id,
        *identity_resolver_);
    if (!context) return context.error();
    if (request_header.operation_id != accessibility_broker_operation_claim ||
        message.handle_count() != 0U) {
        return os::ipc::send_rpc_error(channel, request_header, protocol_error());
    }

    // Caller authorization deliberately precedes target decoding and endpoint
    // lookup. An ordinary peer therefore cannot use malformed/unknown target
    // responses as an application/session enumeration oracle.
    if (context.value().peer.principal !=
        os::accessibility::accessibility_service_principal) {
        return os::ipc::send_rpc_error(
            channel,
            request_header,
            manager_error(manager_errors::accessibility_authority_denied));
    }

    auto target = decode_claim_request(message.payload());
    if (!target) return os::ipc::send_rpc_error(channel, request_header, target.error());

    auto endpoint = backend_.claim(
        backend_.context,
        context.value().peer,
        target.value());
    if (!endpoint) {
        return os::ipc::send_rpc_error(channel, request_header, endpoint.error());
    }
    if (!endpoint.value().valid() || endpoint.value().application != target.value()) {
        return os::ipc::send_rpc_error(channel, request_header, protocol_error());
    }

    auto encoded = encode_claim_response(endpoint.value(), scratch);
    if (!encoded) return os::ipc::send_rpc_error(channel, request_header, encoded.error());

    auto transfer = endpoint.value().channel.take_native_handle_for_transfer();
    if (!transfer.valid()) {
        return os::ipc::send_rpc_error(
            channel,
            request_header,
            manager_error(manager_errors::accessibility_endpoint_unavailable));
    }
    const std::array<os::core::NativeHandle, 1U> handles{std::move(transfer)};
    return os::ipc::send_rpc_response(
        channel,
        request_header,
        {scratch.data(), encoded.value()},
        std::span<const os::core::NativeHandle>{handles});
}

os::core::Result<BrokeredAccessibilityEndpoint> AccessibilityBrokerControlClient::claim(
    os::core::PeerIdentity application,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, accessibility_broker_claim_request_size_v1> request{};
    auto encoded = encode_claim_request(application, request);
    if (!encoded) return encoded.error();

    auto response = connection_.call(
        accessibility_broker_control_service_id,
        accessibility_broker_operation_claim,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    auto message = std::move(response).value();
    if (message.handle_count() != 1U) return protocol_error();

    auto metadata = decode_claim_response(message.payload());
    if (!metadata || metadata.value().second != application) {
        return protocol_error();
    }
    auto handle = message.take_handle(0U);
    if (!handle) return handle.error();
    auto channel = os::ipc::Channel::adopt(std::move(handle).value());
    if (!channel) return channel.error();

    BrokeredAccessibilityEndpoint endpoint{};
    endpoint.session_id = metadata.value().first;
    endpoint.application = metadata.value().second;
    endpoint.channel = std::move(channel).value();
    if (!endpoint.valid()) return protocol_error();
    return endpoint;
}

} // namespace os::app
