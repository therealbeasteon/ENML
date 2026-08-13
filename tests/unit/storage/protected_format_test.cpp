#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/storage/protected_format.hpp>

int main() {
    constexpr os::storage::ProtectedChunkHeaderV2 header{
        .crypto_profile = os::keys::CryptoProfileId::aes_256_gcm_v1,
        .user = os::core::UserId{42U},
        .object_id = os::storage::ProtectedObjectId{
            0x1122334455667788ULL,
            0x99AABBCCDDEEFF00ULL,
        },
        .object_generation = 9U,
        .chunk_index = 7U,
        .plaintext_size = 4096U,
        .flags = 0U,
    };
    static_assert(os::storage::valid_protected_chunk_header(header));

    std::array<std::byte, os::storage::protected_chunk_header_bytes> encoded{};
    auto written = os::storage::encode_protected_chunk_header_v2(header, encoded);
    assert(written);
    assert(written.value() == encoded.size());

    auto parsed = os::storage::decode_protected_chunk_header_v2(encoded);
    assert(parsed);
    assert(parsed.value().crypto_profile == header.crypto_profile);
    assert(parsed.value().user == header.user);
    assert(parsed.value().object_id == header.object_id);
    assert(parsed.value().object_generation == header.object_generation);
    assert(parsed.value().chunk_index == header.chunk_index);
    assert(parsed.value().plaintext_size == header.plaintext_size);

    auto different_generation = encoded;
    different_generation[36U] = static_cast<std::byte>(10U);
    auto parsed_generation = os::storage::decode_protected_chunk_header_v2(different_generation);
    assert(parsed_generation);
    assert(parsed_generation.value().object_generation != header.object_generation);

    auto different_chunk = encoded;
    different_chunk[44U] = static_cast<std::byte>(8U);
    auto parsed_chunk = os::storage::decode_protected_chunk_header_v2(different_chunk);
    assert(parsed_chunk);
    assert(parsed_chunk.value().chunk_index != header.chunk_index);

    auto bad_version = encoded;
    bad_version[4U] = static_cast<std::byte>(1U);
    assert(!os::storage::decode_protected_chunk_header_v2(bad_version));

    auto bad_flags = encoded;
    bad_flags[56U] = static_cast<std::byte>(1U);
    assert(!os::storage::decode_protected_chunk_header_v2(bad_flags));

    os::storage::ProtectedChunkHeaderV2 no_generation = header;
    no_generation.object_generation = 0U;
    assert(!os::storage::valid_protected_chunk_header(no_generation));

    os::storage::ProtectedChunkHeaderV2 too_large = header;
    too_large.plaintext_size =
        static_cast<std::uint32_t>(os::storage::protected_chunk_plaintext_bytes + 1U);
    assert(!os::storage::valid_protected_chunk_header(too_large));

    return 0;
}
