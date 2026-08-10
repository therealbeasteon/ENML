#include <os/app/shell_lifecycle_control.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <os/core/error.hpp>
#include <os/core/platform_principals.hpp>
#include <os/ipc/encoder.hpp>

namespace os::app {
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

[[nodiscard]] os::core::Result<void> encode_application(
    os::ipc::Encoder& encoder,
    const os::package::ApplicationIdentity& application) noexcept {
    if (!application.valid()) return protocol_error();
    auto result = encoder.write_utf8(
        application.package_id.view(),
        os::package::max_package_id_bytes);
    if (!result) return result.error();
    return encoder.write_raw({
        application.signer_lineage.bytes.data(),
        application.signer_lineage.bytes.size(),
    });
}

[[nodiscard]] bool snapshot_valid(const ApplicationLifecycleSnapshot& snapshot) noexcept {
    if (snapshot.revision == 0U || snapshot.count > snapshot.applications.size()) return false;

    std::uint64_t previous_instance = 0U;
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const auto& record = snapshot.applications[index];
        if (!record.valid() || record.instance.value() <= previous_instance) return false;
        previous_instance = record.instance.value();

        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            const auto& previous = snapshot.applications[earlier];
            if (previous.instance == record.instance || previous.identity == record.identity) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] os::core::Result<std::size_t> encode_snapshot(
    const ApplicationLifecycleSnapshot& snapshot,
    os::core::MutableByteSpan output) noexcept {
    if (!snapshot_valid(snapshot)) return protocol_error();

    os::ipc::Encoder encoder{output};
    auto result = encoder.write_u64_le(snapshot.revision);
    if (!result) return result.error();
    result = encoder.write_u16_le(static_cast<std::uint16_t>(snapshot.count));
    if (!result) return result.error();

    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const auto& record = snapshot.applications[index];
        result = encoder.write_u64_le(record.instance.value());
        if (!result) return result.error();
        result = encode_application(encoder, record.application);
        if (!result) return result.error();
        result = encode_identity(encoder, record.identity);
        if (!result) return result.error();
    }
    return encoder.written().size();
}

[[nodiscard]] os::core::Result<ApplicationLifecycleSnapshot> manager_snapshot(
    void* context) noexcept {
    if (context == nullptr) {
        return service_error(os::core::errors::service::invalid_request);
    }
    return static_cast<ApplicationManager*>(context)->lifecycle_snapshot();
}

} // namespace

ShellLifecycleBackend shell_lifecycle_backend(ApplicationManager& manager) noexcept {
    return ShellLifecycleBackend{
        .context = &manager,
        .snapshot = manager_snapshot,
    };
}

os::core::Result<void> ShellLifecycleControlServer::dispatch_once(
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
        shell_lifecycle_control_service_id,
        *identity_resolver_);
    if (!context) return context.error();

    // Authority is checked before operation/payload validation. An ordinary
    // process that happens to possess the endpoint gets one indistinguishable
    // denial and cannot probe lifecycle shape, application count, capability
    // availability or protocol details through differential errors.
    if (context.value().peer.principal != os::core::shell_service_principal) {
        return os::ipc::send_rpc_error(
            channel,
            request_header,
            service_error(os::core::errors::service::access_denied));
    }

    if (message.handle_count() != 0U || !message.payload().empty()) {
        return os::ipc::send_rpc_error(channel, request_header, protocol_error());
    }

    if (request_header.operation_id == shell_lifecycle_operation_snapshot) {
        auto snapshot = backend_.snapshot(backend_.context);
        if (!snapshot) {
            return os::ipc::send_rpc_error(channel, request_header, snapshot.error());
        }
        auto encoded = encode_snapshot(snapshot.value(), scratch);
        if (!encoded) {
            return os::ipc::send_rpc_error(channel, request_header, encoded.error());
        }
        return os::ipc::send_rpc_response(
            channel,
            request_header,
            {scratch.data(), encoded.value()});
    }

    if (request_header.operation_id == shell_lifecycle_operation_take_compositor) {
        if (backend_.take_compositor_capability == nullptr) {
            return os::ipc::send_rpc_error(
                channel,
                request_header,
                service_error(os::core::errors::service::not_supported));
        }
        auto capability = backend_.take_compositor_capability(
            backend_.compositor_context);
        if (!capability) {
            return os::ipc::send_rpc_error(channel, request_header, capability.error());
        }
        if (!capability.value().valid()) {
            return os::ipc::send_rpc_error(channel, request_header, protocol_error());
        }
        std::array<os::core::NativeHandle, 1U> handles{
            std::move(capability).value(),
        };
        return os::ipc::send_rpc_response(
            channel,
            request_header,
            {},
            std::span<const os::core::NativeHandle>{handles.data(), handles.size()});
    }

    return os::ipc::send_rpc_error(channel, request_header, protocol_error());
}

} // namespace os::app
