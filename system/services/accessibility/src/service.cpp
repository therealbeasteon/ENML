#include <os/accessibility/service.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <os/core/error.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::accessibility {
namespace {

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error protocol_error() noexcept {
    return os::core::make_error(
        os::core::ErrorDomain::ipc,
        os::ipc::errors::protocol_violation);
}

[[nodiscard]] constexpr bool peer_died(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::ipc &&
        error.code == os::ipc::errors::peer_died;
}

[[nodiscard]] constexpr bool action_valid(os::ui::UiAction action) noexcept {
    switch (action) {
    case os::ui::UiAction::activate:
    case os::ui::UiAction::focus:
    case os::ui::UiAction::toggle:
    case os::ui::UiAction::select:
        return true;
    case os::ui::UiAction::set_text:
        return false;
    }
    return false;
}

[[nodiscard]] os::core::Result<std::size_t> encode_action_request(
    const AccessibilityServiceActionRequest& request,
    os::core::MutableByteSpan output) noexcept {
    if (!os::core::valid_peer_identity(request.application) ||
        request.snapshot_revision == 0U || request.target.value() == 0U ||
        !action_valid(request.action) ||
        output.size() < accessibility_service_action_request_size_v1) {
        return protocol_error();
    }

    auto identity = encode_broker_claim_request_v1(
        request.application,
        output.first(accessibility_service_identity_request_size_v1));
    if (!identity) return identity.error();

    os::ipc::Encoder encoder{
        output.subspan(accessibility_service_identity_request_size_v1)};
    auto result = encoder.write_u64_le(request.snapshot_revision);
    if (!result) return result.error();
    result = encoder.write_u32_le(request.target.value());
    if (!result) return result.error();
    result = encoder.write_u16_le(static_cast<std::uint16_t>(request.action));
    if (!result) return result.error();
    if (accessibility_service_identity_request_size_v1 + encoder.written().size() !=
        accessibility_service_action_request_size_v1) {
        return protocol_error();
    }
    return accessibility_service_action_request_size_v1;
}

[[nodiscard]] os::core::Result<AccessibilityServiceActionRequest> decode_action_request(
    os::core::ByteSpan payload) noexcept {
    if (payload.size() != accessibility_service_action_request_size_v1) {
        return protocol_error();
    }
    auto application = decode_broker_claim_request_v1(
        payload.first(accessibility_service_identity_request_size_v1));
    if (!application) return application.error();

    os::ipc::Decoder decoder{
        payload.subspan(accessibility_service_identity_request_size_v1)};
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto target = decoder.read_u32_le();
    if (!target) return target.error();
    auto action = decoder.read_u16_le();
    if (!action) return action.error();
    auto end = decoder.require_end();
    if (!end) return protocol_error();

    const AccessibilityServiceActionRequest request{
        .application = application.value(),
        .snapshot_revision = revision.value(),
        .target = os::ui::UiNodeId{target.value()},
        .action = static_cast<os::ui::UiAction>(action.value()),
    };
    if (request.snapshot_revision == 0U || request.target.value() == 0U ||
        !action_valid(request.action)) {
        return protocol_error();
    }
    return request;
}

} // namespace

AccessibilityServiceRuntime::SessionSlot* AccessibilityServiceRuntime::find(
    os::core::PeerIdentity application) noexcept {
    for (auto& slot : sessions_) {
        if (slot.occupied && slot.application == application) return &slot;
    }
    return nullptr;
}

const AccessibilityServiceRuntime::SessionSlot* AccessibilityServiceRuntime::find(
    os::core::PeerIdentity application) const noexcept {
    for (const auto& slot : sessions_) {
        if (slot.occupied && slot.application == application) return &slot;
    }
    return nullptr;
}

std::size_t AccessibilityServiceRuntime::session_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : sessions_) {
        if (slot.occupied) ++count;
    }
    return count;
}

os::core::Result<AccessibilityBrokerClaimMetadata> AccessibilityServiceRuntime::claim(
    os::core::PeerIdentity application,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid() || !os::core::valid_peer_identity(application)) {
        return service_error(service_errors::malformed_request);
    }
    if (find(application) != nullptr) {
        return service_error(service_errors::duplicate_session);
    }

    SessionSlot* free_slot = nullptr;
    for (auto& slot : sessions_) {
        if (!slot.occupied) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) {
        return service_error(service_errors::session_capacity);
    }

    auto claimed = broker_.claim(application, scratch);
    if (!claimed) return claimed.error();
    auto endpoint = std::move(claimed).value();
    if (!endpoint.valid() || endpoint.application != application) {
        return protocol_error();
    }

    const AccessibilityBrokerClaimMetadata metadata{
        .session_id = endpoint.session_id,
        .application = endpoint.application,
    };
    free_slot->occupied = true;
    free_slot->session_id = endpoint.session_id;
    free_slot->application = endpoint.application;
    free_slot->channel = std::move(endpoint.channel);
    return metadata;
}

os::core::Result<void> AccessibilityServiceRuntime::snapshot(
    os::core::PeerIdentity application,
    os::ui::AccessibilitySessionSnapshot& output,
    os::core::MutableByteSpan scratch) noexcept {
    auto* slot = find(application);
    if (slot == nullptr || !slot->channel.valid()) {
        return service_error(service_errors::unknown_session);
    }

    AccessibilitySessionClient client{slot->channel};
    auto result = client.snapshot(
        os::ui::AccessibilitySessionId{slot->session_id},
        output,
        scratch);
    if (!result && peer_died(result.error())) {
        *slot = SessionSlot{};
    }
    return result;
}

os::core::Result<os::ui::UiEvent> AccessibilityServiceRuntime::dispatch_action(
    const AccessibilityServiceActionRequest& request,
    os::core::MutableByteSpan scratch) noexcept {
    if (!action_valid(request.action) || request.snapshot_revision == 0U ||
        request.target.value() == 0U) {
        return service_error(service_errors::malformed_request);
    }
    auto* slot = find(request.application);
    if (slot == nullptr || !slot->channel.valid()) {
        return service_error(service_errors::unknown_session);
    }

    AccessibilitySessionClient client{slot->channel};
    const os::ui::AccessibilitySessionActionRequest session_request{
        .session = os::ui::AccessibilitySessionId{slot->session_id},
        .request = os::ui::AccessibilityActionRequest{
            .snapshot_revision = request.snapshot_revision,
            .target = request.target,
            .action = request.action,
        },
    };
    auto result = client.dispatch_action(session_request, scratch);
    if (!result && peer_died(result.error())) {
        *slot = SessionSlot{};
    }
    return result;
}

os::core::Result<void> AccessibilityServiceRuntime::release(
    os::core::PeerIdentity application) noexcept {
    auto* slot = find(application);
    if (slot == nullptr) return service_error(service_errors::unknown_session);
    *slot = SessionSlot{};
    return {};
}

os::core::Result<AccessibilityBrokerClaimMetadata> AccessibilityServiceClient::claim(
    os::core::PeerIdentity application,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, accessibility_service_identity_request_size_v1> request{};
    auto encoded = encode_broker_claim_request_v1(application, request);
    if (!encoded) return encoded.error();
    auto response = connection_.call(
        accessibility_service_id,
        accessibility_service_op_claim,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    auto message = std::move(response).value();
    if (message.handle_count() != 0U) return protocol_error();
    auto metadata = decode_broker_claim_response_v1(message.payload());
    if (!metadata || metadata.value().application != application) return protocol_error();
    return metadata.value();
}

os::core::Result<void> AccessibilityServiceClient::snapshot(
    os::core::PeerIdentity application,
    os::ui::AccessibilitySessionSnapshot& output,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, accessibility_service_identity_request_size_v1> request{};
    auto encoded = encode_broker_claim_request_v1(application, request);
    if (!encoded) return encoded.error();
    auto response = connection_.call(
        accessibility_service_id,
        accessibility_service_op_snapshot,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    auto message = std::move(response).value();
    if (message.handle_count() != 0U) return protocol_error();
    return decode_snapshot_response_v1(message.payload(), output);
}

os::core::Result<os::ui::UiEvent> AccessibilityServiceClient::dispatch_action(
    const AccessibilityServiceActionRequest& request,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, accessibility_service_action_request_size_v1> payload{};
    auto encoded = encode_action_request(request, payload);
    if (!encoded) return encoded.error();
    auto response = connection_.call(
        accessibility_service_id,
        accessibility_service_op_action,
        {payload.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    auto message = std::move(response).value();
    if (message.handle_count() != 0U) return protocol_error();
    return decode_action_response_v1(message.payload());
}

os::core::Result<void> AccessibilityServiceClient::release(
    os::core::PeerIdentity application,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, accessibility_service_identity_request_size_v1> request{};
    auto encoded = encode_broker_claim_request_v1(application, request);
    if (!encoded) return encoded.error();
    auto response = connection_.call(
        accessibility_service_id,
        accessibility_service_op_release,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    auto message = std::move(response).value();
    if (message.handle_count() != 0U || !message.payload().empty()) return protocol_error();
    return {};
}

os::core::Result<void> AccessibilityServiceServer::dispatch_once(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid() || !channel.valid()) {
        return service_error(service_errors::malformed_request);
    }

    auto received = channel.receive(scratch);
    if (!received) return received.error();
    auto message = std::move(received).value();
    const auto request_header = message.header();

    auto context = os::ipc::validate_rpc_request(
        message,
        accessibility_service_id,
        *identity_resolver_);
    if (!context) return context.error();
    if (message.handle_count() != 0U) {
        return os::ipc::send_rpc_error(channel, request_header, protocol_error());
    }
    if (context.value().peer.principal != admin_principal_) {
        return os::ipc::send_rpc_error(
            channel,
            request_header,
            service_error(service_errors::authority_denied));
    }

    switch (request_header.operation_id) {
    case accessibility_service_op_claim: {
        auto application = decode_broker_claim_request_v1(message.payload());
        if (!application) {
            return os::ipc::send_rpc_error(channel, request_header, application.error());
        }
        auto claimed = runtime_->claim(application.value(), scratch);
        if (!claimed) {
            return os::ipc::send_rpc_error(channel, request_header, claimed.error());
        }
        auto encoded = encode_broker_claim_response_v1(claimed.value(), scratch);
        if (!encoded) {
            return os::ipc::send_rpc_error(channel, request_header, encoded.error());
        }
        return os::ipc::send_rpc_response(
            channel,
            request_header,
            {scratch.data(), encoded.value()});
    }
    case accessibility_service_op_snapshot: {
        auto application = decode_broker_claim_request_v1(message.payload());
        if (!application) {
            return os::ipc::send_rpc_error(channel, request_header, application.error());
        }
        os::ui::AccessibilitySessionSnapshot snapshot{};
        auto captured = runtime_->snapshot(application.value(), snapshot, scratch);
        if (!captured) {
            return os::ipc::send_rpc_error(channel, request_header, captured.error());
        }
        auto encoded = encode_snapshot_response_v1(snapshot, scratch);
        if (!encoded) {
            return os::ipc::send_rpc_error(channel, request_header, encoded.error());
        }
        return os::ipc::send_rpc_response(
            channel,
            request_header,
            {scratch.data(), encoded.value()});
    }
    case accessibility_service_op_action: {
        auto request = decode_action_request(message.payload());
        if (!request) {
            return os::ipc::send_rpc_error(channel, request_header, request.error());
        }
        auto event = runtime_->dispatch_action(request.value(), scratch);
        if (!event) {
            return os::ipc::send_rpc_error(channel, request_header, event.error());
        }
        auto encoded = encode_action_response_v1(event.value(), scratch);
        if (!encoded) {
            return os::ipc::send_rpc_error(channel, request_header, encoded.error());
        }
        return os::ipc::send_rpc_response(
            channel,
            request_header,
            {scratch.data(), encoded.value()});
    }
    case accessibility_service_op_release: {
        auto application = decode_broker_claim_request_v1(message.payload());
        if (!application) {
            return os::ipc::send_rpc_error(channel, request_header, application.error());
        }
        auto released = runtime_->release(application.value());
        if (!released) {
            return os::ipc::send_rpc_error(channel, request_header, released.error());
        }
        return os::ipc::send_rpc_response(channel, request_header, {});
    }
    default:
        return os::ipc::send_rpc_error(
            channel,
            request_header,
            os::core::make_error(
                os::core::ErrorDomain::service,
                os::core::errors::service::unknown_operation));
    }
}

} // namespace os::accessibility
