#include <os/display/service.hpp>

#include <array>
#include <bit>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <poll.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <os/display/error.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::display {
namespace {

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] os::core::Result<void> encode_identity(
    os::ipc::Encoder& encoder,
    const os::core::PeerIdentity& identity) noexcept {
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
    if (!os::core::valid_peer_identity(identity)) return display_error(errors::invalid_identity);
    return identity;
}

[[nodiscard]] os::core::Result<void> encode_rect(
    os::ipc::Encoder& encoder,
    Rect rect) noexcept {
    auto result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(rect.x));
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(rect.y));
    if (!result) return result.error();
    result = encoder.write_u32_le(rect.width);
    if (!result) return result.error();
    return encoder.write_u32_le(rect.height);
}

[[nodiscard]] os::core::Result<Rect> decode_rect(os::ipc::Decoder& decoder) noexcept {
    auto x = decoder.read_u32_le();
    if (!x) return x.error();
    auto y = decoder.read_u32_le();
    if (!y) return y.error();
    auto width = decoder.read_u32_le();
    if (!width) return width.error();
    auto height = decoder.read_u32_le();
    if (!height) return height.error();
    return Rect{
        .x = std::bit_cast<std::int32_t>(x.value()),
        .y = std::bit_cast<std::int32_t>(y.value()),
        .width = width.value(),
        .height = height.value(),
    };
}

[[nodiscard]] os::core::Result<void> encode_surface(
    os::ipc::Encoder& encoder,
    const SurfaceDescriptor& surface) noexcept {
    auto result = encoder.write_u64_le(surface.id.value());
    if (!result) return result.error();
    result = encode_identity(encoder, surface.owner);
    if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(surface.role));
    if (!result) return result.error();
    result = encoder.write_u64_le(surface.parent.value());
    if (!result) return result.error();
    result = encode_rect(encoder, surface.bounds);
    if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(surface.visibility));
    if (!result) return result.error();
    return encoder.write_u32_le(surface.accepts_input ? 1U : 0U);
}

[[nodiscard]] os::core::Result<SurfaceDescriptor> decode_surface(
    os::ipc::Decoder& decoder) noexcept {
    auto id = decoder.read_u64_le();
    if (!id) return id.error();
    auto identity = decode_identity(decoder);
    if (!identity) return identity.error();
    auto role = decoder.read_u32_le();
    if (!role) return role.error();
    auto parent = decoder.read_u64_le();
    if (!parent) return parent.error();
    auto bounds = decode_rect(decoder);
    if (!bounds) return bounds.error();
    auto visibility = decoder.read_u32_le();
    if (!visibility) return visibility.error();
    auto accepts = decoder.read_u32_le();
    if (!accepts) return accepts.error();
    if (role.value() < static_cast<std::uint32_t>(SurfaceRole::application) ||
        role.value() > static_cast<std::uint32_t>(SurfaceRole::secure_system) ||
        visibility.value() > static_cast<std::uint32_t>(SurfaceVisibility::visible) ||
        accepts.value() > 1U) {
        return service_error(os::core::errors::service::invalid_request);
    }
    const SurfaceDescriptor surface{
        .id = SurfaceId{id.value()},
        .owner = identity.value(),
        .role = static_cast<SurfaceRole>(role.value()),
        .parent = SurfaceId{parent.value()},
        .bounds = bounds.value(),
        .visibility = static_cast<SurfaceVisibility>(visibility.value()),
        .accepts_input = accepts.value() != 0U,
    };
    if (!surface.valid()) return service_error(os::core::errors::service::invalid_request);
    return surface;
}

[[nodiscard]] os::core::Result<void> encode_buffer(
    os::ipc::Encoder& encoder,
    const BufferDescriptor& buffer) noexcept {
    auto result = encoder.write_u64_le(buffer.id.value());
    if (!result) return result.error();
    result = encode_identity(encoder, buffer.owner);
    if (!result) return result.error();
    result = encoder.write_u32_le(buffer.size.width);
    if (!result) return result.error();
    result = encoder.write_u32_le(buffer.size.height);
    if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(buffer.format));
    if (!result) return result.error();
    result = encoder.write_u32_le(buffer.stride_bytes);
    if (!result) return result.error();
    return encoder.write_u64_le(buffer.byte_size);
}

[[nodiscard]] os::core::Result<BufferDescriptor> decode_buffer(
    os::ipc::Decoder& decoder) noexcept {
    auto id = decoder.read_u64_le();
    if (!id) return id.error();
    auto identity = decode_identity(decoder);
    if (!identity) return identity.error();
    auto width = decoder.read_u32_le();
    if (!width) return width.error();
    auto height = decoder.read_u32_le();
    if (!height) return height.error();
    auto format = decoder.read_u32_le();
    if (!format) return format.error();
    auto stride = decoder.read_u32_le();
    if (!stride) return stride.error();
    auto bytes = decoder.read_u64_le();
    if (!bytes) return bytes.error();
    if (format.value() < static_cast<std::uint32_t>(PixelFormat::rgba8888) ||
        format.value() > static_cast<std::uint32_t>(PixelFormat::rgbx8888)) {
        return service_error(os::core::errors::service::invalid_request);
    }
    const BufferDescriptor buffer{
        .id = BufferId{id.value()},
        .owner = identity.value(),
        .size = {width.value(), height.value()},
        .format = static_cast<PixelFormat>(format.value()),
        .stride_bytes = stride.value(),
        .byte_size = bytes.value(),
    };
    if (!buffer.valid()) return service_error(os::core::errors::service::invalid_request);
    return buffer;
}

[[nodiscard]] constexpr TrustedPresentation trusted_presentation_for_role(
    SurfaceRole role) noexcept {
    switch (role) {
    case SurfaceRole::application:
    case SurfaceRole::popup:
        return TrustedPresentation::none;
    case SurfaceRole::system_chrome:
        return TrustedPresentation::system_chrome;
    case SurfaceRole::secure_system:
        return TrustedPresentation::secure_system;
    }
    return TrustedPresentation::none;
}

[[nodiscard]] os::core::Result<void> encode_input_hit(
    os::ipc::Encoder& encoder,
    const SurfaceInputHit& hit) noexcept {
    if (!hit.valid() || hit.trusted_presentation != trusted_presentation_for_role(hit.role)) {
        return service_error(os::core::errors::service::invalid_request);
    }
    auto result = encoder.write_u64_le(hit.surface.value());
    if (!result) return result.error();
    result = encode_identity(encoder, hit.owner);
    if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(hit.role));
    if (!result) return result.error();
    result = encoder.write_u32_le(hit.surface_size.width);
    if (!result) return result.error();
    result = encoder.write_u32_le(hit.surface_size.height);
    if (!result) return result.error();
    result = encoder.write_u64_le(hit.frame_sequence);
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(hit.local_x));
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(hit.local_y));
    if (!result) return result.error();
    return encoder.write_u32_le(static_cast<std::uint32_t>(hit.trusted_presentation));
}

[[nodiscard]] os::core::Result<SurfaceInputHit> decode_input_hit(
    os::ipc::Decoder& decoder) noexcept {
    auto surface = decoder.read_u64_le();
    if (!surface) return surface.error();
    auto owner = decode_identity(decoder);
    if (!owner) return owner.error();
    auto role = decoder.read_u32_le();
    if (!role) return role.error();
    auto width = decoder.read_u32_le();
    if (!width) return width.error();
    auto height = decoder.read_u32_le();
    if (!height) return height.error();
    auto frame = decoder.read_u64_le();
    if (!frame) return frame.error();
    auto local_x = decoder.read_u32_le();
    if (!local_x) return local_x.error();
    auto local_y = decoder.read_u32_le();
    if (!local_y) return local_y.error();
    auto trusted = decoder.read_u32_le();
    if (!trusted) return trusted.error();
    if (role.value() < static_cast<std::uint32_t>(SurfaceRole::application) ||
        role.value() > static_cast<std::uint32_t>(SurfaceRole::secure_system) ||
        trusted.value() > static_cast<std::uint32_t>(TrustedPresentation::secure_system)) {
        return service_error(os::core::errors::service::invalid_request);
    }
    const SurfaceInputHit hit{
        .surface = SurfaceId{surface.value()},
        .owner = owner.value(),
        .role = static_cast<SurfaceRole>(role.value()),
        .surface_size = {width.value(), height.value()},
        .frame_sequence = frame.value(),
        .local_x = std::bit_cast<std::int32_t>(local_x.value()),
        .local_y = std::bit_cast<std::int32_t>(local_y.value()),
        .trusted_presentation = static_cast<TrustedPresentation>(trusted.value()),
    };
    if (!hit.valid() || hit.trusted_presentation != trusted_presentation_for_role(hit.role)) {
        return service_error(os::core::errors::service::invalid_request);
    }
    return hit;
}

[[nodiscard]] os::core::Result<void> require_empty_payload(
    const os::ipc::InboundMessage& message) noexcept {
    if (!message.payload().empty()) return service_error(os::core::errors::service::invalid_request);
    return {};
}

[[nodiscard]] os::core::Result<void> send_empty(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request) noexcept {
    return os::ipc::send_rpc_response(channel, request, {});
}

[[nodiscard]] std::uint64_t monotonic_now_ns() noexcept {
    timespec now{};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 || now.tv_nsec < 0) return 0U;
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL +
        static_cast<std::uint64_t>(now.tv_nsec);
}

[[nodiscard]] bool pidfd_dead(const os::core::NativeHandle& pidfd) noexcept {
    if (!pidfd.valid()) return true;
    pollfd descriptor{.fd = pidfd.native(), .events = POLLIN, .revents = 0};
    int result = 0;
    do {
        result = ::poll(&descriptor, 1U, 0);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0;
}

} // namespace

os::core::Result<void> CompositorService::note_client(
    os::core::PeerIdentity peer,
    os::ipc::KernelPeerCredentials kernel) noexcept {
    prune_dead_clients();
    for (auto& entry : clients_) {
        if (entry.occupied && entry.peer == peer) {
            if (entry.kernel != kernel) {
                return os::core::make_error(
                    os::core::ErrorDomain::security,
                    os::core::errors::security::credential_mismatch);
            }
            return {};
        }
    }
    if (kernel.process_id <= 0 || kernel.process_id > INT_MAX) {
        return os::core::make_error(
            os::core::ErrorDomain::security,
            os::core::errors::security::invalid_identity);
    }
    const long opened = ::syscall(SYS_pidfd_open, static_cast<int>(kernel.process_id), 0U);
    if (opened < 0 || opened > INT_MAX) {
        return os::core::make_error(
            os::core::ErrorDomain::security,
            os::core::errors::security::stale_process);
    }
    os::core::NativeHandle pidfd{static_cast<int>(opened)};
    if (pidfd_dead(pidfd)) {
        return os::core::make_error(
            os::core::ErrorDomain::security,
            os::core::errors::security::stale_process);
    }
    for (auto& entry : clients_) {
        if (!entry.occupied) {
            entry.occupied = true;
            entry.peer = peer;
            entry.kernel = kernel;
            entry.pidfd = std::move(pidfd);
            return {};
        }
    }
    return os::core::make_error(
        os::core::ErrorDomain::security,
        os::core::errors::security::registry_full);
}

void CompositorService::revoke_process(os::core::ProcessId process) noexcept {
    if (process.value() == 0U) return;
    compositor_->revoke_process(process);
    buffers_->revoke_process(process);
    for (auto& entry : clients_) {
        if (entry.occupied && entry.peer.process == process) entry = ClientEntry{};
    }
}

void CompositorService::prune_dead_clients() noexcept {
    for (auto& entry : clients_) {
        if (entry.occupied && pidfd_dead(entry.pidfd)) {
            const auto process = entry.peer.process;
            compositor_->revoke_process(process);
            buffers_->revoke_process(process);
            entry = ClientEntry{};
        }
    }
}

os::core::Result<void> CompositorService::dispatch_once(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan scratch) noexcept {
    if (compositor_ == nullptr || buffers_ == nullptr || identity_resolver_ == nullptr) {
        return display_error(errors::invalid_configuration);
    }
    prune_dead_clients();
    auto received = channel.receive(scratch);
    if (!received) return received.error();
    auto message = std::move(received).value();
    auto context = os::ipc::validate_rpc_request(message, compositor_service_id, *identity_resolver_);
    if (!context) {
        auto sent = os::ipc::send_rpc_error(channel, message.header(), context.error());
        if (!sent) return sent.error();
        return {};
    }
    auto client = note_client(context.value().peer, message.sender_credentials());
    if (!client) {
        auto sent = os::ipc::send_rpc_error(channel, message.header(), client.error());
        if (!sent) return sent.error();
        return {};
    }

    const auto fail = [&](os::core::Error error) noexcept -> os::core::Result<void> {
        return os::ipc::send_rpc_error(channel, message.header(), error);
    };

    switch (message.header().operation_id) {
    case compositor_op_get_configuration: {
        auto empty = require_empty_payload(message);
        if (!empty) return fail(empty.error());
        const auto& configuration = compositor_->configuration();
        std::array<std::byte, 36U> payload{};
        os::ipc::Encoder encoder{payload};
        auto result = encoder.write_u32_le(configuration.size.width);
        if (!result) return result.error();
        result = encoder.write_u32_le(configuration.size.height);
        if (!result) return result.error();
        result = encoder.write_u32_le(configuration.safe_insets.top);
        if (!result) return result.error();
        result = encoder.write_u32_le(configuration.safe_insets.right);
        if (!result) return result.error();
        result = encoder.write_u32_le(configuration.safe_insets.bottom);
        if (!result) return result.error();
        result = encoder.write_u32_le(configuration.safe_insets.left);
        if (!result) return result.error();
        result = encoder.write_u32_le(configuration.refresh_millihz);
        if (!result) return result.error();
        result = encoder.write_u64_le(configuration.compositor_margin_ns);
        if (!result) return result.error();
        return os::ipc::send_rpc_response(channel, message.header(), encoder.written());
    }
    case compositor_op_create_surface: {
        os::ipc::Decoder decoder{message.payload()};
        auto role = decoder.read_u32_le();
        if (!role) return fail(role.error());
        auto parent = decoder.read_u64_le();
        if (!parent) return fail(parent.error());
        auto bounds = decode_rect(decoder);
        if (!bounds) return fail(bounds.error());
        auto accepts = decoder.read_u32_le();
        if (!accepts) return fail(accepts.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        if (role.value() < static_cast<std::uint32_t>(SurfaceRole::application) ||
            role.value() > static_cast<std::uint32_t>(SurfaceRole::secure_system) || accepts.value() > 1U) {
            return fail(service_error(os::core::errors::service::invalid_request));
        }
        auto created = compositor_->create_surface(context.value().peer, {
            .role = static_cast<SurfaceRole>(role.value()),
            .parent = SurfaceId{parent.value()},
            .bounds = bounds.value(),
            .accepts_input = accepts.value() != 0U,
        });
        if (!created) return fail(created.error());
        std::array<std::byte, 80U> payload{};
        os::ipc::Encoder encoder{payload};
        auto encoded = encode_surface(encoder, created.value());
        if (!encoded) return encoded.error();
        return os::ipc::send_rpc_response(channel, message.header(), encoder.written());
    }
    case compositor_op_destroy_surface:
    case compositor_op_activate_application: {
        os::ipc::Decoder decoder{message.payload()};
        auto surface = decoder.read_u64_le();
        if (!surface) return fail(surface.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        auto result = message.header().operation_id == compositor_op_destroy_surface
            ? compositor_->destroy_surface(context.value().peer, SurfaceId{surface.value()})
            : compositor_->activate_application(context.value().peer, SurfaceId{surface.value()});
        if (!result) return fail(result.error());
        return send_empty(channel, message.header());
    }
    case compositor_op_set_bounds: {
        os::ipc::Decoder decoder{message.payload()};
        auto surface = decoder.read_u64_le();
        if (!surface) return fail(surface.error());
        auto bounds = decode_rect(decoder);
        if (!bounds) return fail(bounds.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        auto result = compositor_->set_bounds(context.value().peer, SurfaceId{surface.value()}, bounds.value());
        if (!result) return fail(result.error());
        return send_empty(channel, message.header());
    }
    case compositor_op_set_visibility: {
        os::ipc::Decoder decoder{message.payload()};
        auto surface = decoder.read_u64_le();
        if (!surface) return fail(surface.error());
        auto visibility = decoder.read_u32_le();
        if (!visibility) return fail(visibility.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        if (visibility.value() > static_cast<std::uint32_t>(SurfaceVisibility::visible)) {
            return fail(service_error(os::core::errors::service::invalid_request));
        }
        auto result = compositor_->set_visibility(
            context.value().peer,
            SurfaceId{surface.value()},
            static_cast<SurfaceVisibility>(visibility.value()));
        if (!result) return fail(result.error());
        return send_empty(channel, message.header());
    }
    case compositor_op_allocate_buffer: {
        os::ipc::Decoder decoder{message.payload()};
        auto width = decoder.read_u32_le();
        if (!width) return fail(width.error());
        auto height = decoder.read_u32_le();
        if (!height) return fail(height.error());
        auto format = decoder.read_u32_le();
        if (!format) return fail(format.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        if (format.value() < static_cast<std::uint32_t>(PixelFormat::rgba8888) ||
            format.value() > static_cast<std::uint32_t>(PixelFormat::rgbx8888)) {
            return fail(display_error(errors::invalid_pixel_format));
        }
        auto allocated = buffers_->allocate(
            context.value().peer,
            {width.value(), height.value()},
            static_cast<PixelFormat>(format.value()));
        if (!allocated) return fail(allocated.error());
        auto lease = std::move(allocated).value();
        std::array<std::byte, 64U> payload{};
        os::ipc::Encoder encoder{payload};
        auto encoded = encode_buffer(encoder, lease.descriptor);
        if (!encoded) return encoded.error();
        const std::array handles{std::move(lease.memory)};
        return os::ipc::send_rpc_response(
            channel,
            message.header(),
            encoder.written(),
            std::span<const os::core::NativeHandle>{handles.data(), handles.size()});
    }
    case compositor_op_release_buffer: {
        os::ipc::Decoder decoder{message.payload()};
        auto buffer = decoder.read_u64_le();
        if (!buffer) return fail(buffer.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        auto released = buffers_->release(context.value().peer, BufferId{buffer.value()});
        if (!released) return fail(released.error());
        compositor_->invalidate_buffer(BufferId{buffer.value()});
        return send_empty(channel, message.header());
    }
    case compositor_op_submit_frame: {
        os::ipc::Decoder decoder{message.payload()};
        auto surface = decoder.read_u64_le();
        if (!surface) return fail(surface.error());
        auto buffer = decoder.read_u64_le();
        if (!buffer) return fail(buffer.error());
        auto sequence = decoder.read_u64_le();
        if (!sequence) return fail(sequence.error());
        auto slot = decoder.read_u32_le();
        if (!slot) return fail(slot.error());
        auto damage_count = decoder.read_u32_le();
        if (!damage_count) return fail(damage_count.error());
        if (slot.value() > std::numeric_limits<std::uint8_t>::max() ||
            damage_count.value() > max_damage_rectangles) {
            return fail(service_error(os::core::errors::service::invalid_request));
        }
        FrameSubmission submission{
            .surface = SurfaceId{surface.value()},
            .buffer = BufferId{buffer.value()},
            .sequence = sequence.value(),
            .buffer_slot = static_cast<std::uint8_t>(slot.value()),
            .damage_count = static_cast<std::uint8_t>(damage_count.value()),
        };
        for (std::size_t index = 0U; index < submission.damage_count; ++index) {
            auto damage = decode_rect(decoder);
            if (!damage) return fail(damage.error());
            submission.damage[index] = damage.value();
        }
        auto end = decoder.require_end();
        if (!end) return fail(end.error());

        auto owned_buffer = buffers_->lookup_owned(context.value().peer, submission.buffer);
        if (!owned_buffer) return fail(owned_buffer.error());
        auto target = compositor_->lookup(submission.surface);
        if (!target) return fail(target.error());
        if (target.value().owner != context.value().peer) return fail(display_error(errors::owner_mismatch));
        if (owned_buffer.value().size.width != target.value().bounds.width ||
            owned_buffer.value().size.height != target.value().bounds.height) {
            return fail(display_error(errors::buffer_size_mismatch));
        }
        const std::uint64_t now_ns = monotonic_now_ns();
        if (now_ns == 0U) return fail(display_error(errors::invalid_configuration));
        auto submitted = compositor_->submit_frame(context.value().peer, submission, now_ns);
        if (!submitted) return fail(submitted.error());

        std::array<std::byte, 40U> payload{};
        os::ipc::Encoder encoder{payload};
        auto result = encoder.write_u64_le(submitted.value().surface.value());
        if (!result) return result.error();
        result = encoder.write_u64_le(submitted.value().buffer.value());
        if (!result) return result.error();
        result = encoder.write_u64_le(submitted.value().sequence);
        if (!result) return result.error();
        result = encoder.write_u64_le(submitted.value().deadline.next_vsync_ns);
        if (!result) return result.error();
        result = encoder.write_u64_le(submitted.value().deadline.submission_deadline_ns);
        if (!result) return result.error();
        return os::ipc::send_rpc_response(channel, message.header(), encoder.written());
    }
    case compositor_op_input_hit_test: {
        os::ipc::Decoder decoder{message.payload()};
        auto global_x = decoder.read_u32_le();
        if (!global_x) return fail(global_x.error());
        auto global_y = decoder.read_u32_le();
        if (!global_y) return fail(global_y.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        auto hit = input_authority_.hit_test(
            context.value().peer,
            std::bit_cast<std::int32_t>(global_x.value()),
            std::bit_cast<std::int32_t>(global_y.value()));
        if (!hit) return fail(hit.error());
        std::array<std::byte, 72U> payload{};
        os::ipc::Encoder encoder{payload};
        auto encoded = encode_input_hit(encoder, hit.value());
        if (!encoded) return encoded.error();
        return os::ipc::send_rpc_response(channel, message.header(), encoder.written());
    }
    case compositor_op_input_validate: {
        os::ipc::Decoder decoder{message.payload()};
        auto hit = decode_input_hit(decoder);
        if (!hit) return fail(hit.error());
        auto end = decoder.require_end();
        if (!end) return fail(end.error());
        auto validated = input_authority_.validate_before_delivery(context.value().peer, hit.value());
        if (!validated) return fail(validated.error());
        return send_empty(channel, message.header());
    }
    default:
        return fail(service_error(os::core::errors::service::unknown_operation));
    }
}

os::core::Result<DisplayConfiguration> CompositorClient::configuration(
    os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    auto response = connection_->call(compositor_service_id, compositor_op_get_configuration, {}, scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    os::ipc::Decoder decoder{response.value().payload()};
    auto width = decoder.read_u32_le(); if (!width) return width.error();
    auto height = decoder.read_u32_le(); if (!height) return height.error();
    auto top = decoder.read_u32_le(); if (!top) return top.error();
    auto right = decoder.read_u32_le(); if (!right) return right.error();
    auto bottom = decoder.read_u32_le(); if (!bottom) return bottom.error();
    auto left = decoder.read_u32_le(); if (!left) return left.error();
    auto refresh = decoder.read_u32_le(); if (!refresh) return refresh.error();
    auto margin = decoder.read_u64_le(); if (!margin) return margin.error();
    auto end = decoder.require_end(); if (!end) return end.error();
    return DisplayConfiguration{
        .size = {width.value(), height.value()},
        .safe_insets = {.top = top.value(), .right = right.value(), .bottom = bottom.value(), .left = left.value()},
        .refresh_millihz = refresh.value(),
        .compositor_margin_ns = margin.value(),
    };
}

os::core::Result<SurfaceDescriptor> CompositorClient::create_surface(
    const CreateSurfaceRequest& request,
    os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 32U> payload{};
    os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u32_le(static_cast<std::uint32_t>(request.role)); if (!result) return result.error();
    result = encoder.write_u64_le(request.parent.value()); if (!result) return result.error();
    result = encode_rect(encoder, request.bounds); if (!result) return result.error();
    result = encoder.write_u32_le(request.accepts_input ? 1U : 0U); if (!result) return result.error();
    auto response = connection_->call(compositor_service_id, compositor_op_create_surface, encoder.written(), scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    os::ipc::Decoder decoder{response.value().payload()};
    auto surface = decode_surface(decoder); if (!surface) return surface.error();
    auto end = decoder.require_end(); if (!end) return end.error();
    return surface.value();
}

os::core::Result<void> CompositorClient::destroy_surface(SurfaceId surface, os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 8U> payload{}; os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u64_le(surface.value()); if (!result) return result.error();
    auto response = connection_->call(compositor_service_id, compositor_op_destroy_surface, encoder.written(), scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    return {};
}

os::core::Result<void> CompositorClient::set_bounds(SurfaceId surface, Rect bounds, os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 24U> payload{}; os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u64_le(surface.value()); if (!result) return result.error();
    result = encode_rect(encoder, bounds); if (!result) return result.error();
    auto response = connection_->call(compositor_service_id, compositor_op_set_bounds, encoder.written(), scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    return {};
}

os::core::Result<void> CompositorClient::set_visibility(SurfaceId surface, SurfaceVisibility visibility, os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 12U> payload{}; os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u64_le(surface.value()); if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(visibility)); if (!result) return result.error();
    auto response = connection_->call(compositor_service_id, compositor_op_set_visibility, encoder.written(), scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    return {};
}

os::core::Result<void> CompositorClient::activate_application(SurfaceId surface, os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 8U> payload{}; os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u64_le(surface.value()); if (!result) return result.error();
    auto response = connection_->call(compositor_service_id, compositor_op_activate_application, encoder.written(), scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    return {};
}

os::core::Result<SharedBufferLease> CompositorClient::allocate_buffer(
    PixelSize size,
    PixelFormat format,
    os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 12U> payload{}; os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u32_le(size.width); if (!result) return result.error();
    result = encoder.write_u32_le(size.height); if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(format)); if (!result) return result.error();
    auto response = connection_->call(compositor_service_id, compositor_op_allocate_buffer, encoder.written(), scratch);
    if (!response) return response.error();
    auto message = std::move(response).value();
    if (message.handle_count() != 1U) return service_error(os::core::errors::service::invalid_request);
    os::ipc::Decoder decoder{message.payload()};
    auto descriptor = decode_buffer(decoder); if (!descriptor) return descriptor.error();
    auto end = decoder.require_end(); if (!end) return end.error();
    auto handle = message.take_handle(0U); if (!handle) return handle.error();
    return SharedBufferLease{.descriptor = descriptor.value(), .memory = std::move(handle).value()};
}

os::core::Result<void> CompositorClient::release_buffer(BufferId buffer, os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 8U> payload{}; os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u64_le(buffer.value()); if (!result) return result.error();
    auto response = connection_->call(compositor_service_id, compositor_op_release_buffer, encoder.written(), scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    return {};
}

os::core::Result<FrameReceipt> CompositorClient::submit_frame(
    const FrameSubmission& submission,
    os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 160U> payload{}; os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u64_le(submission.surface.value()); if (!result) return result.error();
    result = encoder.write_u64_le(submission.buffer.value()); if (!result) return result.error();
    result = encoder.write_u64_le(submission.sequence); if (!result) return result.error();
    result = encoder.write_u32_le(submission.buffer_slot); if (!result) return result.error();
    result = encoder.write_u32_le(submission.damage_count); if (!result) return result.error();
    if (submission.damage_count > max_damage_rectangles) return display_error(errors::invalid_damage);
    for (std::size_t index = 0U; index < submission.damage_count; ++index) {
        result = encode_rect(encoder, submission.damage[index]); if (!result) return result.error();
    }
    auto response = connection_->call(compositor_service_id, compositor_op_submit_frame, encoder.written(), scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) return service_error(os::core::errors::service::invalid_request);
    os::ipc::Decoder decoder{response.value().payload()};
    auto surface = decoder.read_u64_le(); if (!surface) return surface.error();
    auto buffer = decoder.read_u64_le(); if (!buffer) return buffer.error();
    auto sequence = decoder.read_u64_le(); if (!sequence) return sequence.error();
    auto vsync = decoder.read_u64_le(); if (!vsync) return vsync.error();
    auto deadline = decoder.read_u64_le(); if (!deadline) return deadline.error();
    auto end = decoder.require_end(); if (!end) return end.error();
    return FrameReceipt{
        .surface = SurfaceId{surface.value()},
        .buffer = BufferId{buffer.value()},
        .sequence = sequence.value(),
        .deadline = {.next_vsync_ns = vsync.value(), .submission_deadline_ns = deadline.value()},
    };
}

os::core::Result<SurfaceInputHit> InputCompositorClient::hit_test(
    std::int32_t global_x,
    std::int32_t global_y,
    os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 8U> payload{};
    os::ipc::Encoder encoder{payload};
    auto result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(global_x));
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(global_y));
    if (!result) return result.error();
    auto response = connection_->call(
        compositor_service_id,
        compositor_op_input_hit_test,
        encoder.written(),
        scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) {
        return service_error(os::core::errors::service::invalid_request);
    }
    os::ipc::Decoder decoder{response.value().payload()};
    auto hit = decode_input_hit(decoder);
    if (!hit) return hit.error();
    auto end = decoder.require_end();
    if (!end) return end.error();
    return hit.value();
}

os::core::Result<void> InputCompositorClient::validate_before_delivery(
    const SurfaceInputHit& hit,
    os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return service_error(os::core::errors::service::invalid_request);
    std::array<std::byte, 72U> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_input_hit(encoder, hit);
    if (!encoded) return encoded.error();
    auto response = connection_->call(
        compositor_service_id,
        compositor_op_input_validate,
        encoder.written(),
        scratch);
    if (!response) return response.error();
    if (!response.value().payload().empty() || response.value().handle_count() != 0U) {
        return service_error(os::core::errors::service::invalid_request);
    }
    return {};
}

} // namespace os::display
