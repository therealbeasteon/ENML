#include <os/storage/protected_format.hpp>

#include <cstddef>
#include <cstdint>

#include <os/storage/error.hpp>

namespace os::storage {
namespace {

void write_u16_le(os::core::MutableByteSpan out, std::size_t offset, std::uint16_t value) noexcept {
    out[offset] = static_cast<std::byte>(value & 0xFFU);
    out[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write_u32_le(os::core::MutableByteSpan out, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t i = 0U; i < 4U; ++i) {
        out[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

void write_u64_le(os::core::MutableByteSpan out, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i) {
        out[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

[[nodiscard]] std::uint16_t read_u16_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(in[offset]) |
        (std::to_integer<std::uint16_t>(in[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) {
        value |= std::to_integer<std::uint32_t>(in[offset + i]) << (8U * i);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        value |= std::to_integer<std::uint64_t>(in[offset + i]) << (8U * i);
    }
    return value;
}

} // namespace

os::core::Result<std::size_t>
encode_protected_chunk_header_v1(
    const ProtectedChunkHeaderV1& header,
    os::core::MutableByteSpan output) noexcept {
    if (!valid_protected_chunk_header(header)) {
        return storage_error(errors::invalid_options);
    }
    if (output.size() < protected_chunk_header_bytes) {
        return storage_error(errors::too_large);
    }

    write_u32_le(output, 0U, protected_chunk_magic);
    write_u16_le(output, 4U, protected_chunk_version);
    write_u16_le(output, 6U, protected_chunk_header_bytes);
    write_u32_le(output, 8U, static_cast<std::uint32_t>(header.crypto_profile));
    write_u64_le(output, 12U, header.user.value());
    write_u64_le(output, 20U, header.object_id.high);
    write_u64_le(output, 28U, header.object_id.low);
    write_u64_le(output, 36U, header.chunk_index);
    write_u32_le(output, 44U, header.plaintext_size);
    write_u32_le(output, 48U, header.flags);
    return static_cast<std::size_t>(protected_chunk_header_bytes);
}

os::core::Result<ProtectedChunkHeaderV1>
decode_protected_chunk_header_v1(os::core::ByteSpan input) noexcept {
    if (input.size() != protected_chunk_header_bytes) {
        return storage_error(errors::invalid_options);
    }
    if (read_u32_le(input, 0U) != protected_chunk_magic ||
        read_u16_le(input, 4U) != protected_chunk_version ||
        read_u16_le(input, 6U) != protected_chunk_header_bytes) {
        return storage_error(errors::invalid_options);
    }

    ProtectedChunkHeaderV1 header{
        .crypto_profile = static_cast<os::keys::CryptoProfileId>(read_u32_le(input, 8U)),
        .user = os::core::UserId{read_u64_le(input, 12U)},
        .object_id = ProtectedObjectId{read_u64_le(input, 20U), read_u64_le(input, 28U)},
        .chunk_index = read_u64_le(input, 36U),
        .plaintext_size = read_u32_le(input, 44U),
        .flags = read_u32_le(input, 48U),
    };
    if (!valid_protected_chunk_header(header)) {
        return storage_error(errors::invalid_options);
    }
    return header;
}

} // namespace os::storage
