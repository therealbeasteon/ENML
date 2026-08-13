#include <os/boot/profile_protector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace os::boot {
namespace {

void write_u16_le(os::core::MutableByteSpan output, std::size_t offset, std::uint16_t value) noexcept {
    output[offset] = static_cast<std::byte>(value & 0xFFU);
    output[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write_u32_le(os::core::MutableByteSpan output, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) {
        output[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64_le(os::core::MutableByteSpan output, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        output[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::uint16_t read_u16_le(os::core::ByteSpan input, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(static_cast<std::uint8_t>(input[offset])) |
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(input[offset + 1U])) << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(os::core::ByteSpan input, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(input[offset + index])) <<
            (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64_le(os::core::ByteSpan input, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(input[offset + index])) <<
            (index * 8U);
    }
    return value;
}

[[nodiscard]] bool nonzero_measurement(const MeasurementDigest& measurement) noexcept {
    std::uint8_t aggregate = 0U;
    for (const auto byte : measurement) aggregate |= byte;
    return aggregate != 0U;
}

} // namespace

bool valid_profile_protector_header(const ProfileProtectorHeaderV1& header) noexcept {
    return header.user.value() != 0U &&
        header.security_epoch.valid() &&
        nonzero_measurement(header.boot_measurement) &&
        header.gate_slot.valid() &&
        header.generation.valid() &&
        header.provider_blob_size != 0U &&
        header.provider_blob_size <= max_profile_protector_blob_bytes;
}

os::core::Result<std::size_t>
encode_profile_protector_header_v1(
    const ProfileProtectorHeaderV1& header,
    os::core::MutableByteSpan output) noexcept {
    if (!valid_profile_protector_header(header)) {
        return profile_protector_error(profile_protector_errors::invalid_header);
    }
    if (output.size() < profile_protector_header_bytes) {
        return profile_protector_error(profile_protector_errors::buffer_too_small);
    }

    write_u32_le(output, 0U, profile_protector_magic);
    write_u16_le(output, 4U, profile_protector_header_bytes);
    write_u16_le(output, 6U, profile_protector_version);
    write_u64_le(output, 8U, header.user.value());
    write_u64_le(output, 16U, header.security_epoch.value);
    for (std::size_t index = 0U; index < header.boot_measurement.size(); ++index) {
        output[24U + index] = static_cast<std::byte>(header.boot_measurement[index]);
    }
    write_u64_le(output, 56U, header.gate_slot.value);
    write_u64_le(output, 64U, header.generation.value);
    write_u32_le(output, 72U, header.provider_blob_size);
    write_u32_le(output, 76U, 0U);
    return static_cast<std::size_t>(profile_protector_header_bytes);
}

os::core::Result<ProfileProtectorRecordView>
parse_profile_protector_record_v1(os::core::ByteSpan record) noexcept {
    if (record.size() < profile_protector_header_bytes ||
        record.size() > max_profile_protector_record_bytes) {
        return profile_protector_error(profile_protector_errors::invalid_header);
    }
    if (read_u32_le(record, 0U) != profile_protector_magic ||
        read_u16_le(record, 4U) != profile_protector_header_bytes) {
        return profile_protector_error(profile_protector_errors::invalid_header);
    }
    if (read_u16_le(record, 6U) != profile_protector_version) {
        return profile_protector_error(profile_protector_errors::unsupported_version);
    }
    if (read_u32_le(record, 76U) != 0U) {
        return profile_protector_error(profile_protector_errors::invalid_header);
    }

    MeasurementDigest measurement{};
    for (std::size_t index = 0U; index < measurement.size(); ++index) {
        measurement[index] = static_cast<std::uint8_t>(record[24U + index]);
    }

    const ProfileProtectorHeaderV1 header{
        .user = os::core::UserId{read_u64_le(record, 8U)},
        .security_epoch = SecurityEpochValue{read_u64_le(record, 16U)},
        .boot_measurement = measurement,
        .gate_slot = CredentialGateSlotId{read_u64_le(record, 56U)},
        .generation = ProtectorGeneration{read_u64_le(record, 64U)},
        .provider_blob_size = read_u32_le(record, 72U),
    };

    if (header.user.value() == 0U) {
        return profile_protector_error(profile_protector_errors::invalid_user);
    }
    if (!header.security_epoch.valid()) {
        return profile_protector_error(profile_protector_errors::invalid_epoch);
    }
    if (!nonzero_measurement(header.boot_measurement)) {
        return profile_protector_error(profile_protector_errors::degenerate_measurement);
    }
    if (!header.gate_slot.valid()) {
        return profile_protector_error(profile_protector_errors::invalid_gate_slot);
    }
    if (!header.generation.valid()) {
        return profile_protector_error(profile_protector_errors::invalid_generation);
    }
    if (header.provider_blob_size == 0U ||
        header.provider_blob_size > max_profile_protector_blob_bytes) {
        return profile_protector_error(profile_protector_errors::invalid_blob_size);
    }

    const std::size_t expected_size =
        static_cast<std::size_t>(profile_protector_header_bytes) +
        static_cast<std::size_t>(header.provider_blob_size);
    if (record.size() != expected_size) {
        return profile_protector_error(profile_protector_errors::invalid_blob_size);
    }

    return ProfileProtectorRecordView{
        .header = header,
        .authenticated_header = record.first(profile_protector_header_bytes),
        .provider_blob = record.subspan(profile_protector_header_bytes, header.provider_blob_size),
    };
}

} // namespace os::boot
