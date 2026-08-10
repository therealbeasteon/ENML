#include <os/display/shell_control.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <os/core/error.hpp>
#include <os/core/platform_principals.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::display {
namespace {

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
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

[[nodiscard]] os::core::Result<void> compositor_activate(
    void* context,
    os::core::PeerIdentity caller,
    os::core::PeerIdentity expected_owner,
    SurfaceId surface) noexcept {
    if (context == nullptr) return service_error(os::core::errors::service::invalid_request);
    return static_cast<Compositor*>(context)->activate_application_exact(
        caller,
        expected_owner,
        surface);
}

} // namespace

ShellCompositorBackend shell_compositor_backend(Compositor& compositor) noexcept {
    return ShellCompositorBackend{
        .context = &compositor,
        .activate_exact = compositor_activate,
    };
}

os::core::Result<void> ShellCompositorControlServer::dispatch_once(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid() || !channel.valid()) {
        return service_error(os::core::errors::service::invalid_request);
    }

    auto received = channel.receive(scratch);
    if (!received) return received.error();
    auto message = std::move(received).value();
    const auto request_header = message.header();

    auto context = os::ipc::validate_rpc_request(
        message,
        shell_compositor_control_service_id,
        *identity_resolver_);
    if (!context) return context.error();

    // Deny before decoding operation-specific data. Endpoint possession,
    // ServiceId knowledge, and a guessed SurfaceId remain insufficient to learn
    // which application/process owns a compositor root.
    if (context.value().peer.principal != os::core::shell_service_principal) {
        return os::ipc::send_rpc_error(
            channel,
            request_header,
            service_error(os::core::errors::service::access_denied));
    }

    if (request_header.operation_id != shell_compositor_operation_activate_exact ||
        message.handle_count() != 0U ||
        message.payload().size() != shell_compositor_activate_request_size_v1) {
        return os::ipc::send_rpc_error(channel, request_header, protocol_error());
    }

    os::ipc::Decoder decoder{message.payload()};
    auto expected_owner = decode_identity(decoder);
    if (!expected_owner) {
        return os::ipc::send_rpc_error(channel, request_header, expected_owner.error());
    }
    auto surface = decoder.read_u64_le();
    if (!surface) return os::ipc::send_rpc_error(channel, request_header, surface.error());
    auto end = decoder.require_end();
    if (!end || !valid_display_object_value(surface.value())) {
        return os::ipc::send_rpc_error(channel, request_header, protocol_error());
    }

    auto activated = backend_.activate_exact(
        backend_.context,
        context.value().peer,
        expected_owner.value(),
        SurfaceId{surface.value()});
    if (!activated) {
        return os::ipc::send_rpc_error(channel, request_header, activated.error());
    }
    return os::ipc::send_rpc_response(channel, request_header, {});
}

os::core::Result<void> ShellCompositorControlClient::activate_exact(
    os::core::PeerIdentity expected_owner,
    SurfaceId application_surface,
    os::core::MutableByteSpan scratch) noexcept {
    if (!os::core::valid_peer_identity(expected_owner) ||
        !valid_display_object_value(application_surface.value())) {
        return protocol_error();
    }

    std::array<std::byte, shell_compositor_activate_request_size_v1> request{};
    os::ipc::Encoder encoder{request};
    auto encoded = encode_identity(encoder, expected_owner);
    if (!encoded) return encoded.error();
    encoded = encoder.write_u64_le(application_surface.value());
    if (!encoded || encoder.written().size() != request.size()) return protocol_error();

    auto response = connection_.call(
        shell_compositor_control_service_id,
        shell_compositor_operation_activate_exact,
        encoder.written(),
        scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) {
        return protocol_error();
    }
    return {};
}

} // namespace os::display
