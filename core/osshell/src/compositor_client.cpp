#include <os/shell/compositor_client.hpp>

#include <array>
#include <cstddef>
#include <utility>

#include <os/core/error.hpp>
#include <os/ipc/encoder.hpp>

namespace os::shell {
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

[[nodiscard]] os::core::Result<void> commit_exact(
    void* context,
    os::core::PeerIdentity expected_owner,
    os::display::SurfaceId root_surface) noexcept {
    auto* activation = static_cast<ShellCompositorActivationContext*>(context);
    if (activation == nullptr || !activation->valid()) return protocol_error();
    return activation->client->activate_exact(
        expected_owner,
        root_surface,
        activation->scratch);
}

} // namespace

os::core::Result<void> ShellCompositorClient::activate_exact(
    os::core::PeerIdentity expected_owner,
    os::display::SurfaceId application_surface,
    os::core::MutableByteSpan scratch) noexcept {
    if (!os::core::valid_peer_identity(expected_owner) ||
        !os::display::valid_display_object_value(application_surface.value()) ||
        scratch.empty()) {
        return protocol_error();
    }

    std::array<std::byte, os::display::shell_compositor_activate_request_size_v1> request{};
    os::ipc::Encoder encoder{request};
    auto encoded = encode_identity(encoder, expected_owner);
    if (!encoded) return encoded.error();
    encoded = encoder.write_u64_le(application_surface.value());
    if (!encoded || encoder.written().size() != request.size()) return protocol_error();

    auto response = connection_.call(
        os::display::shell_compositor_control_service_id,
        os::display::shell_compositor_operation_activate_exact,
        encoder.written(),
        scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) {
        return protocol_error();
    }
    return {};
}

ExactActivationBackend shell_compositor_activation_backend(
    ShellCompositorActivationContext& context) noexcept {
    if (!context.valid()) return {};
    return ExactActivationBackend{
        .context = &context,
        .activate = commit_exact,
    };
}

} // namespace os::shell
