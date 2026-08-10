#include <os/app/input_event.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/wire.hpp>

namespace os::app {
namespace {

inline constexpr os::core::ServiceId application_input_event_service_id{0x0000F012U};
inline constexpr std::uint32_t application_input_event_operation_pointer = 1U;

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] constexpr bool phase_valid(ApplicationPointerPhase phase) noexcept {
    switch (phase) {
    case ApplicationPointerPhase::down:
    case ApplicationPointerPhase::move:
    case ApplicationPointerPhase::up:
    case ApplicationPointerPhase::cancel:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] os::core::Result<void> encode_event(
    os::ipc::Encoder& encoder,
    const ApplicationInputEventV1& event) noexcept {
    if (!event.valid()) return service_error(os::core::errors::service::invalid_request);

    auto result = encoder.write_u16_le(application_input_event_version_v1);
    if (!result) return result.error();
    result = encoder.write_u16_le(application_input_event_payload_size_v1);
    if (!result) return result.error();
    result = encoder.write_u64_le(event.sequence);
    if (!result) return result.error();
    result = encoder.write_u64_le(event.target.principal.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(event.target.principal.low);
    if (!result) return result.error();
    result = encoder.write_u64_le(event.target.user.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(event.target.process.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(event.surface_id);
    if (!result) return result.error();
    result = encoder.write_u64_le(event.frame_sequence);
    if (!result) return result.error();
    result = encoder.write_u32_le(event.surface_width_px);
    if (!result) return result.error();
    result = encoder.write_u32_le(event.surface_height_px);
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(event.local_x_px));
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(event.local_y_px));
    if (!result) return result.error();
    result = encoder.write_u32_le(event.pointer_id);
    if (!result) return result.error();
    return encoder.write_u32_le(static_cast<std::uint32_t>(event.phase));
}

[[nodiscard]] os::core::Result<ApplicationInputEventV1> decode_event(
    os::core::ByteSpan payload) noexcept {
    if (payload.size() != application_input_event_payload_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    auto sequence = decoder.read_u64_le();
    if (!sequence) return sequence.error();
    auto principal_high = decoder.read_u64_le();
    if (!principal_high) return principal_high.error();
    auto principal_low = decoder.read_u64_le();
    if (!principal_low) return principal_low.error();
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto process = decoder.read_u64_le();
    if (!process) return process.error();
    auto surface = decoder.read_u64_le();
    if (!surface) return surface.error();
    auto frame = decoder.read_u64_le();
    if (!frame) return frame.error();
    auto width = decoder.read_u32_le();
    if (!width) return width.error();
    auto height = decoder.read_u32_le();
    if (!height) return height.error();
    auto local_x = decoder.read_u32_le();
    if (!local_x) return local_x.error();
    auto local_y = decoder.read_u32_le();
    if (!local_y) return local_y.error();
    auto pointer = decoder.read_u32_le();
    if (!pointer) return pointer.error();
    auto phase = decoder.read_u32_le();
    if (!phase) return phase.error();
    auto end = decoder.require_end();
    if (!end) return ipc_error(os::ipc::errors::protocol_violation);

    if (version.value() != application_input_event_version_v1 ||
        size.value() != application_input_event_payload_size_v1 ||
        phase.value() < static_cast<std::uint32_t>(ApplicationPointerPhase::down) ||
        phase.value() > static_cast<std::uint32_t>(ApplicationPointerPhase::cancel)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    const ApplicationInputEventV1 event{
        .sequence = sequence.value(),
        .target = {
            .principal = {principal_high.value(), principal_low.value()},
            .user = os::core::UserId{user.value()},
            .process = os::core::ProcessId{process.value()},
        },
        .surface_id = surface.value(),
        .frame_sequence = frame.value(),
        .surface_width_px = width.value(),
        .surface_height_px = height.value(),
        .local_x_px = std::bit_cast<std::int32_t>(local_x.value()),
        .local_y_px = std::bit_cast<std::int32_t>(local_y.value()),
        .pointer_id = pointer.value(),
        .phase = static_cast<ApplicationPointerPhase>(phase.value()),
    };
    if (!event.valid()) return ipc_error(os::ipc::errors::protocol_violation);
    return event;
}

} // namespace

bool ApplicationInputEventV1::valid() const noexcept {
    if (sequence == 0U || !os::core::valid_peer_identity(target) ||
        surface_id == 0U || frame_sequence == 0U ||
        surface_width_px == 0U || surface_height_px == 0U ||
        local_x_px < 0 || local_y_px < 0 || !phase_valid(phase)) {
        return false;
    }
    return static_cast<std::uint64_t>(local_x_px) < surface_width_px &&
        static_cast<std::uint64_t>(local_y_px) < surface_height_px;
}

os::core::Result<void> send_application_input_event(
    os::ipc::Channel& channel,
    const ApplicationInputEventV1& event) noexcept {
    if (!channel.valid() || !event.valid()) {
        return service_error(os::core::errors::service::invalid_request);
    }
    std::array<std::byte, application_input_event_payload_size_v1> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_event(encoder, event);
    if (!encoded) return encoded.error();

    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::event),
        .service_id = application_input_event_service_id,
        .operation_id = application_input_event_operation_pointer,
        .request_id = {},
        .payload_size = static_cast<std::uint32_t>(encoder.written().size()),
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    return channel.send(header, encoder.written());
}

os::core::Result<ApplicationInputEventV1> ApplicationInputEventStream::receive(
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid()) return service_error(os::core::errors::service::invalid_request);

    auto received = channel_->receive(scratch);
    if (!received) return received.error();
    auto message = std::move(received).value();
    const auto& header = message.header();
    const bool header_valid =
        has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        !has_flag(header.flags, os::ipc::WireFlag::has_handles) &&
        header.service_id == application_input_event_service_id &&
        header.operation_id == application_input_event_operation_pointer &&
        header.request_id.value() == 0U &&
        header.handle_count == 0U && message.handle_count() == 0U;
    if (!header_valid) return ipc_error(os::ipc::errors::protocol_violation);

    auto event = decode_event(message.payload());
    if (!event) return event.error();
    if (event.value().target != expected_identity_) {
        return security_error(os::core::errors::security::credential_mismatch);
    }
    if (event.value().sequence <= last_sequence_) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    last_sequence_ = event.value().sequence;
    return event.value();
}

} // namespace os::app
