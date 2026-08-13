#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/storage/protected_format.hpp>

int main() {
    constexpr os::storage::ProtectedChunkHeaderV1 header{
        .crypto_profile = os::keys::CryptoProfileId::aes_256_gcm_v1,
        .user = os::core::UserId{42U},
        .object_id = os::storage::ProtectedObjectId{
            0x1122334455667788ULL,
            0x99AABBCCDDEEFF00ULL,
        },
        .chunk_index = 7U,
        .plaintext_size = 4096U,
        .flags = 0U,
    };
    static_assert(os::storage::valid_protected_chunk_header(header));

    std::array<std::byte, os::storage::protected_chunk_header_bytes> encoded{};
    auto written = os::storage::encode_protected_chunk_header_v1(header, encoded);
    assert(written);
    assert(written.value() == encoded.size());

    auto parsed = os::storage::decode_protected_chunk_header_v1(encoded);
    assert(parsed);
    assert(parsed.value().crypto_profile == header.crypto_profile);
    assert(parsed.value().user == header.user);
    assert(parsed.value().object_id == header.object_id);
    assert(parsed.value().chunk_index == header.chunk_index);
    assert(parsed.value().plaintext_size == header.plaintext_size);

    // Cross-profile substitution changes authenticated metadata even when the
    // object/chunk identity is otherwise identical.
    auto different_user = encoded;
    different_user[12U] = static_cast<std::byte>(43U);
    auto parsed_other = os::storage::decode_protected_chunk_header_v1(different_user);
    assert(parsed_other);
    assert(parsed_other.value().user != header.user);
    assert(different_user != encoded);

    // Chunk relocation likewise changes AAD. The AEAD layer will therefore
    // reject copying ciphertext from one chunk position to another.
    auto different_chunk = encoded;
    different_chunk[36U] = static_cast<std::byte>(8U);
    auto parsed_chunk = os::storage::decode_protected_chunk_header_v1(different_chunk);
    assert(parsed_chunk);
    assert(parsed_chunk.value().chunk_index != header.chunk_index);
    assert(different_chunk != encoded);

    // Unsupported profile/version/flags and oversized plaintext fail closed.
    auto bad_version = encoded;
    bad_version[4U] = static_cast<std::byte>(2U);
    assert(!os::storage::decode_protected_chunk_header_v1(bad_version));

    auto bad_flags = encoded;
    bad_flags[48U] = static_cast<std::byte>(1U);
    assert(!os::storage::decode_protected_chunk_header_v1(bad_flags));

    os::storage::ProtectedChunkHeaderV1 too_large = header;
    too_large.plaintext_size =
        static_cast<std::uint32_t>(os::storage::protected_chunk_plaintext_bytes + 1U);
    assert(!os::storage::valid_protected_chunk_header(too_large));

    os::storage::ProtectedChunkHeaderV1 no_user = header;
    no_user.user = os::core::UserId{};
    assert(!os::storage::valid_protected_chunk_header(no_user));

    os::storage::ProtectedChunkHeaderV1 no_object = header;
    no_object.object_id = {};
    assert(!os::storage::valid_protected_chunk_header(no_object));

    return 0;
}
