#include <os/keys/ciphertext.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/keys/error.hpp>

namespace os::keys {
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

} // namespace

os::core::Result<std::size_t>
encode_ciphertext_header_v1(
    const CiphertextHeaderV1& header,
    os::core::MutableByteSpan output) noexcept {
    if (!header.key_id.valid() || header.key_version == 0U ||
        !valid_crypto_profile(header.profile) ||
        header.ciphertext_size > max_key_plaintext_bytes) {
        return key_error(errors::malformed_ciphertext);
    }
    if (output.size() < ciphertext_header_bytes) {
        return key_error(errors::output_too_small);
    }

    write_u32_le(output, 0U, ciphertext_magic);
    write_u16_le(output, 4U, ciphertext_header_bytes);
    write_u16_le(output, 6U, ciphertext_envelope_version);
    write_u32_le(output, 8U, static_cast<std::uint32_t>(header.profile));
    write_u32_le(output, 12U, header.key_version);
    write_u64_le(output, 16U, header.key_id.high);
    write_u64_le(output, 24U, header.key_id.low);
    write_u16_le(output, 32U, static_cast<std::uint16_t>(aead_nonce_bytes));
    write_u16_le(output, 34U, static_cast<std::uint16_t>(aead_tag_bytes));
    write_u32_le(output, 36U, 0U);
    write_u32_le(output, 40U, header.ciphertext_size);
    return static_cast<std::size_t>(ciphertext_header_bytes);
}

os::core::Result<CiphertextEnvelopeView>
parse_ciphertext_envelope_v1(os::core::ByteSpan envelope) noexcept {
    if (envelope.size() < ciphertext_fixed_overhead ||
        envelope.size() > max_ciphertext_envelope_bytes) {
        return key_error(errors::malformed_ciphertext);
    }

    if (read_u32_le(envelope, 0U) != ciphertext_magic ||
        read_u16_le(envelope, 4U) != ciphertext_header_bytes ||
        read_u16_le(envelope, 6U) != ciphertext_envelope_version ||
        read_u16_le(envelope, 32U) != aead_nonce_bytes ||
        read_u16_le(envelope, 34U) != aead_tag_bytes ||
        read_u32_le(envelope, 36U) != 0U) {
        return key_error(errors::malformed_ciphertext);
    }

    const auto profile = static_cast<CryptoProfileId>(read_u32_le(envelope, 8U));
    if (!valid_crypto_profile(profile)) {
        return key_error(errors::unsupported_crypto_profile);
    }

    const KeyId key_id{
        read_u64_le(envelope, 16U),
        read_u64_le(envelope, 24U),
    };
    const std::uint32_t key_version = read_u32_le(envelope, 12U);
    const std::uint32_t ciphertext_size = read_u32_le(envelope, 40U);
    if (!key_id.valid() || key_version == 0U || ciphertext_size > max_key_plaintext_bytes) {
        return key_error(errors::malformed_ciphertext);
    }

    const std::size_t expected_size =
        ciphertext_fixed_overhead + static_cast<std::size_t>(ciphertext_size);
    if (envelope.size() != expected_size) {
        return key_error(errors::malformed_ciphertext);
    }

    const std::size_t nonce_offset = ciphertext_header_bytes;
    const std::size_t tag_offset = nonce_offset + aead_nonce_bytes;
    const std::size_t ciphertext_offset = tag_offset + aead_tag_bytes;

    return CiphertextEnvelopeView{
        .header = CiphertextHeaderV1{
            .profile = profile,
            .key_id = key_id,
            .key_version = key_version,
            .ciphertext_size = ciphertext_size,
        },
        .authenticated_header = envelope.first(ciphertext_header_bytes),
        .nonce = envelope.subspan(nonce_offset, aead_nonce_bytes),
        .tag = envelope.subspan(tag_offset, aead_tag_bytes),
        .ciphertext = envelope.subspan(ciphertext_offset, ciphertext_size),
    };
}

} // namespace os::keys
