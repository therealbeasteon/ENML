#include <os/accessibility/broker.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <os/core/error.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::accessibility {
namespace {

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

} // namespace

os::core::Result<std::size_t> encode_broker_claim_request_v1(
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

os::core::Result<os::core::PeerIdentity> decode_broker_claim_request_v1(
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

os::core::Result<std::size_t> encode_broker_claim_response_v1(
    AccessibilityBrokerClaimMetadata metadata,
    os::core::MutableByteSpan output) noexcept {
    if (!metadata.valid() || output.size() < accessibility_broker_claim_response_size_v1) {
        return protocol_error();
    }
    os::ipc::Encoder encoder{output};
    auto result = encoder.write_u64_le(metadata.session_id);
    if (!result) return result.error();
    result = encode_identity(encoder, metadata.application);
    if (!result) return result.error();
    if (encoder.written().size() != accessibility_broker_claim_response_size_v1) {
        return protocol_error();
    }
    return encoder.written().size();
}

os::core::Result<AccessibilityBrokerClaimMetadata>
decode_broker_claim_response_v1(os::core::ByteSpan payload) noexcept {
    if (payload.size() != accessibility_broker_claim_response_size_v1) {
        return protocol_error();
    }
    os::ipc::Decoder decoder{payload};
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto application = decode_identity(decoder);
    if (!application) return application.error();
    auto end = decoder.require_end();
    if (!end) return protocol_error();

    const AccessibilityBrokerClaimMetadata metadata{
        .session_id = session.value(),
        .application = application.value(),
    };
    if (!metadata.valid()) return protocol_error();
    return metadata;
}

os::core::Result<BrokeredApplicationSession> AccessibilityBrokerClient::claim(
    os::core::PeerIdentity application,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, accessibility_broker_claim_request_size_v1> request{};
    auto encoded = encode_broker_claim_request_v1(application, request);
    if (!encoded) return encoded.error();

    auto response = connection_.call(
        accessibility_broker_control_service_id,
        accessibility_broker_operation_claim,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    auto message = std::move(response).value();
    if (message.handle_count() != 1U) return protocol_error();

    auto metadata = decode_broker_claim_response_v1(message.payload());
    if (!metadata || metadata.value().application != application) {
        return protocol_error();
    }
    auto handle = message.take_handle(0U);
    if (!handle) return handle.error();
    auto channel = os::ipc::Channel::adopt(std::move(handle).value());
    if (!channel) return channel.error();

    BrokeredApplicationSession endpoint{};
    endpoint.session_id = metadata.value().session_id;
    endpoint.application = metadata.value().application;
    endpoint.channel = std::move(channel).value();
    if (!endpoint.valid()) return protocol_error();
    return endpoint;
}

} // namespace os::accessibility
