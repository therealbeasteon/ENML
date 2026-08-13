#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/boot/profile_protector.hpp>

namespace {

os::boot::MeasurementDigest measurement() {
    os::boot::MeasurementDigest value{};
    for (std::size_t index = 0U; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(index + 1U);
    }
    return value;
}

} // namespace

int main() {
    const os::boot::ProfileProtectorHeaderV1 header{
        .user = os::core::UserId{42U},
        .security_epoch = os::boot::SecurityEpochValue{7U},
        .boot_measurement = measurement(),
        .gate_slot = os::boot::CredentialGateSlotId{11U},
        .generation = os::boot::ProtectorGeneration{3U},
        .provider_blob_size = 16U,
    };

    assert(os::boot::valid_profile_protector_header(header));

    std::array<std::byte, os::boot::max_profile_protector_record_bytes> buffer{};
    auto encoded = os::boot::encode_profile_protector_header_v1(header, buffer);
    assert(encoded);
    assert(encoded.value() == os::boot::profile_protector_header_bytes);

    for (std::size_t index = 0U; index < header.provider_blob_size; ++index) {
        buffer[os::boot::profile_protector_header_bytes + index] =
            static_cast<std::byte>(0xA0U + index);
    }
    const std::size_t record_size =
        os::boot::profile_protector_header_bytes + header.provider_blob_size;

    auto parsed = os::boot::parse_profile_protector_record_v1(
        os::core::ByteSpan{buffer.data(), record_size});
    assert(parsed);
    assert(parsed.value().header.user == header.user);
    assert(parsed.value().header.security_epoch == header.security_epoch);
    assert(parsed.value().header.boot_measurement == header.boot_measurement);
    assert(parsed.value().header.gate_slot == header.gate_slot);
    assert(parsed.value().header.generation == header.generation);
    assert(parsed.value().provider_blob.size() == header.provider_blob_size);
    assert(parsed.value().authenticated_header.size() == os::boot::profile_protector_header_bytes);

    auto short_record = os::boot::parse_profile_protector_record_v1(
        os::core::ByteSpan{buffer.data(), os::boot::profile_protector_header_bytes - 1U});
    assert(!short_record);

    auto wrong_size = os::boot::parse_profile_protector_record_v1(
        os::core::ByteSpan{buffer.data(), record_size - 1U});
    assert(!wrong_size);

    auto malformed = buffer;
    malformed[76U] = std::byte{1U};
    assert(!os::boot::parse_profile_protector_record_v1(
        os::core::ByteSpan{malformed.data(), record_size}));

    auto zero_user = header;
    zero_user.user = os::core::UserId{};
    assert(!os::boot::valid_profile_protector_header(zero_user));
    assert(!os::boot::encode_profile_protector_header_v1(zero_user, buffer));

    auto zero_epoch = header;
    zero_epoch.security_epoch = os::boot::SecurityEpochValue{};
    assert(!os::boot::valid_profile_protector_header(zero_epoch));

    auto zero_measurement = header;
    zero_measurement.boot_measurement = {};
    assert(!os::boot::valid_profile_protector_header(zero_measurement));

    auto zero_slot = header;
    zero_slot.gate_slot = os::boot::CredentialGateSlotId{};
    assert(!os::boot::valid_profile_protector_header(zero_slot));

    auto zero_generation = header;
    zero_generation.generation = os::boot::ProtectorGeneration{};
    assert(!os::boot::valid_profile_protector_header(zero_generation));

    auto oversized_blob = header;
    oversized_blob.provider_blob_size =
        static_cast<std::uint32_t>(os::boot::max_profile_protector_blob_bytes + 1U);
    assert(!os::boot::valid_profile_protector_header(oversized_blob));

    std::array<std::byte, os::boot::profile_protector_header_bytes - 1U> too_small{};
    assert(!os::boot::encode_profile_protector_header_v1(header, too_small));

    return 0;
}
