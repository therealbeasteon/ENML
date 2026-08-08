#include <os/ipc/wire.hpp>

#include <algorithm>
#include <array>

#include <os/core/error.hpp>

namespace os::ipc {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, WireFlag flag) noexcept {
    return (flags & flag_value(flag)) != 0U;
}

inline constexpr std::uint32_t known_flags =
    flag_value(WireFlag::request) |
    flag_value(WireFlag::response) |
    flag_value(WireFlag::event) |
    flag_value(WireFlag::error) |
    flag_value(WireFlag::oneway) |
    flag_value(WireFlag::cancellable) |
    flag_value(WireFlag::has_handles);

} // namespace

os::core::Result<void>
validate_wire_flags(std::uint32_t flags, std::uint16_t handle_count) noexcept {
    if ((flags & ~known_flags) != 0U) {
        return ipc_error(errors::invalid_flags);
    }

    const auto request = has_flag(flags, WireFlag::request);
    const auto response = has_flag(flags, WireFlag::response);
    const auto event = has_flag(flags, WireFlag::event);
    const auto error = has_flag(flags, WireFlag::error);
    const auto oneway = has_flag(flags, WireFlag::oneway);
    const auto cancellable = has_flag(flags, WireFlag::cancellable);
    const auto has_handles = has_flag(flags, WireFlag::has_handles);

    const auto primary_count = static_cast<unsigned int>(request) +
        static_cast<unsigned int>(response) + static_cast<unsigned int>(event);
    if (primary_count != 1U) {
        return ipc_error(errors::invalid_flags);
    }

    if (error && !response) {
        return ipc_error(errors::invalid_flags);
    }
    if (oneway && !request) {
        return ipc_error(errors::invalid_flags);
    }
    if (cancellable && !request) {
        return ipc_error(errors::invalid_flags);
    }
    if (oneway && cancellable) {
        return ipc_error(errors::invalid_flags);
    }
    if ((handle_count > 0U) != has_handles) {
        return ipc_error(errors::invalid_flags);
    }
    return {};
}

os::core::Result<void>
encode_wire_header_v1(Encoder& encoder, const WireHeaderV1& header) noexcept {
    if (encoder.remaining() < wire_header_v1_size) {
        return ipc_error(errors::buffer_too_small);
    }
    if (header.payload_size > max_inline_payload_size) {
        return ipc_error(errors::oversized_message);
    }
    if (header.handle_count > max_handle_count) {
        return ipc_error(errors::oversized_message);
    }
    if (header.payload_checksum != 0U) {
        return ipc_error(errors::unsupported_checksum);
    }
    auto flags_result = validate_wire_flags(header.flags, header.handle_count);
    if (!flags_result) {
        return flags_result.error();
    }

    auto result = encoder.write_raw(wire_magic);
    if (!result) return result.error();
    result = encoder.write_u16_le(wire_header_v1_size);
    if (!result) return result.error();
    result = encoder.write_u16_le(transport_version_v1);
    if (!result) return result.error();
    result = encoder.write_u32_le(header.flags);
    if (!result) return result.error();
    result = encoder.write_u32_le(header.service_id.value());
    if (!result) return result.error();
    result = encoder.write_u32_le(header.operation_id);
    if (!result) return result.error();
    result = encoder.write_u64_le(header.request_id.value());
    if (!result) return result.error();
    result = encoder.write_u32_le(header.payload_size);
    if (!result) return result.error();
    result = encoder.write_u16_le(header.handle_count);
    if (!result) return result.error();
    result = encoder.write_u16_le(0U);
    if (!result) return result.error();
    return encoder.write_u32_le(0U);
}

os::core::Result<WireHeaderV1>
decode_wire_header_v1(Decoder& decoder) noexcept {
    auto magic_result = decoder.read_raw(wire_magic.size());
    if (!magic_result) {
        return magic_result.error();
    }
    if (!std::equal(magic_result.value().begin(), magic_result.value().end(), wire_magic.begin())) {
        return ipc_error(errors::malformed_message);
    }

    auto header_size_result = decoder.read_u16_le();
    if (!header_size_result) return header_size_result.error();
    if (header_size_result.value() != wire_header_v1_size) {
        return ipc_error(errors::malformed_message);
    }

    auto version_result = decoder.read_u16_le();
    if (!version_result) return version_result.error();
    if (version_result.value() != transport_version_v1) {
        return ipc_error(errors::unsupported_version);
    }

    auto flags_result = decoder.read_u32_le();
    if (!flags_result) return flags_result.error();
    auto service_result = decoder.read_u32_le();
    if (!service_result) return service_result.error();
    auto operation_result = decoder.read_u32_le();
    if (!operation_result) return operation_result.error();
    auto request_result = decoder.read_u64_le();
    if (!request_result) return request_result.error();
    auto payload_result = decoder.read_u32_le();
    if (!payload_result) return payload_result.error();
    auto handle_result = decoder.read_u16_le();
    if (!handle_result) return handle_result.error();
    auto reserved_result = decoder.read_u16_le();
    if (!reserved_result) return reserved_result.error();
    if (reserved_result.value() != 0U) {
        return ipc_error(errors::malformed_message);
    }
    auto checksum_result = decoder.read_u32_le();
    if (!checksum_result) return checksum_result.error();
    if (checksum_result.value() != 0U) {
        return ipc_error(errors::unsupported_checksum);
    }

    if (payload_result.value() > max_inline_payload_size) {
        return ipc_error(errors::oversized_message);
    }
    if (handle_result.value() > max_handle_count) {
        return ipc_error(errors::oversized_message);
    }
    auto validation = validate_wire_flags(flags_result.value(), handle_result.value());
    if (!validation) {
        return validation.error();
    }

    return WireHeaderV1{
        .flags = flags_result.value(),
        .service_id = os::core::ServiceId{service_result.value()},
        .operation_id = operation_result.value(),
        .request_id = os::core::RequestId{request_result.value()},
        .payload_size = payload_result.value(),
        .handle_count = handle_result.value(),
        .payload_checksum = 0U,
    };
}

} // namespace os::ipc
