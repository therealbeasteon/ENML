#include <os/accessibility/transport.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/wire.hpp>

namespace os::accessibility {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] bool request_header_valid(const os::ipc::InboundMessage& message) noexcept {
    const auto& header = message.header();
    const bool known_operation =
        header.operation_id == accessibility_session_op_snapshot ||
        header.operation_id == accessibility_session_op_action;
    return has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        !has_flag(header.flags, os::ipc::WireFlag::has_handles) &&
        header.service_id == application_accessibility_session_service_id &&
        known_operation && header.request_id.value() != 0U &&
        header.handle_count == 0U && message.handle_count() == 0U;
}

} // namespace

os::core::Result<void> AccessibilitySessionServer::dispatch_once(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid() || !channel.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::invalid_request);
    }

    auto received = channel.receive(scratch);
    if (!received) return received.error();
    auto message = std::move(received).value();
    if (!request_header_valid(message)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    const auto request_header = message.header();
    const auto operation = request_header.operation_id == accessibility_session_op_snapshot
        ? TransportOperation::snapshot
        : TransportOperation::action;

    // The request payload borrows `scratch`. dispatch_accessibility_transport_v1
    // fully decodes the small request before writing a response into the same
    // caller-owned buffer, so no second 64 KiB allocation is required.
    auto dispatched = dispatch_accessibility_transport_v1(
        *authority_,
        mediated_caller_,
        operation,
        message.payload(),
        scratch);
    if (!dispatched) {
        return os::ipc::send_rpc_error(channel, request_header, dispatched.error());
    }

    return os::ipc::send_rpc_response(
        channel,
        request_header,
        {scratch.data(), dispatched.value()});
}

os::core::Result<void> AccessibilitySessionClient::snapshot(
    os::ui::AccessibilitySessionId session,
    os::ui::AccessibilitySessionSnapshot& output,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, snapshot_request_size_v1> request{};
    auto encoded = encode_snapshot_request_v1(session, request);
    if (!encoded) return encoded.error();

    auto response = connection_.call(
        application_accessibility_session_service_id,
        accessibility_session_op_snapshot,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    auto decoded = decode_snapshot_response_v1(response.value().payload(), output);
    if (!decoded) return decoded.error();
    if (output.session != session) {
        output = {};
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

os::core::Result<os::ui::UiEvent> AccessibilitySessionClient::dispatch_action(
    const os::ui::AccessibilitySessionActionRequest& request,
    os::core::MutableByteSpan scratch) noexcept {
    std::array<std::byte, action_request_size_v1> payload{};
    auto encoded = encode_action_request_v1(request, payload);
    if (!encoded) return encoded.error();

    auto response = connection_.call(
        application_accessibility_session_service_id,
        accessibility_session_op_action,
        {payload.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return decode_action_response_v1(response.value().payload());
}

} // namespace os::accessibility
