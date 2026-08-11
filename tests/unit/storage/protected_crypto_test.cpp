#include <array>
#include <cassert>
#include <cstddef>

#include <os/keys/hierarchy.hpp>
#include <os/keys/testing/openssl_provider.hpp>
#include <os/storage/protected_crypto.hpp>

namespace {

constexpr os::keys::KeyProtectionBinding system_binding{
    .scope = os::keys::KeyProtectionScope::system,
    .owner = os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0x9100000000000001ULL, 0xA100000000000001ULL},
        .user = os::core::UserId{0U},
    },
};
constexpr os::keys::KeyProtectionBinding profile_binding{
    .scope = os::keys::KeyProtectionScope::user_profile,
    .owner = os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0x9200000000000002ULL, 0xA200000000000002ULL},
        .user = os::core::UserId{42U},
    },
};

} // namespace

int main() {
    os::keys::testing::OpenSslTestKeyProvider provider;
    os::keys::KeyHierarchy hierarchy{provider};
    assert(hierarchy.initialize(system_binding));
    assert(hierarchy.ensure_profile(profile_binding));
    auto key = hierarchy.generate_profile_storage_key(os::core::UserId{42U});
    assert(key);

    os::storage::ProtectedChunkCrypto crypto{provider};
    const os::storage::ProtectedChunkHeaderV1 header{
        .crypto_profile = os::keys::CryptoProfileId::aes_256_gcm_v1,
        .user = os::core::UserId{42U},
        .object_id = os::storage::ProtectedObjectId{0x1111U, 0x2222U},
        .chunk_index = 7U,
        .plaintext_size = 5U,
        .flags = 0U,
    };
    const std::array<std::byte, 5U> message{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}, std::byte{0x50},
    };
    std::array<std::byte, os::storage::max_protected_chunk_record_bytes> record{};
    auto sealed = crypto.seal(key.value(), header, message, record);
    assert(sealed);

    std::array<std::byte, 16U> opened{};
    const os::storage::ProtectedChunkAddress expected{
        .user = header.user,
        .object_id = header.object_id,
        .chunk_index = header.chunk_index,
    };
    auto result = crypto.open(
        key.value(), expected, os::core::ByteSpan{record.data(), sealed.value()}, opened);
    assert(result);
    assert(result.value() == message.size());
    for (std::size_t i = 0U; i < message.size(); ++i) assert(opened[i] == message[i]);

    // A complete valid record cannot be transplanted to another object/chunk;
    // expected identity comes from trusted Storage state, not from disk.
    auto wrong_address = expected;
    wrong_address.object_id = os::storage::ProtectedObjectId{0x3333U, 0x4444U};
    assert(!crypto.open(
        key.value(), wrong_address, os::core::ByteSpan{record.data(), sealed.value()}, opened));
    wrong_address = expected;
    wrong_address.chunk_index += 1U;
    assert(!crypto.open(
        key.value(), wrong_address, os::core::ByteSpan{record.data(), sealed.value()}, opened));

    // Header and ciphertext tampering must fail. Header corruption may fail at
    // parsing or at provider authentication; ciphertext corruption must fail AEAD.
    auto tampered = record;
    tampered[20U] ^= std::byte{0x01};
    assert(!crypto.open(
        key.value(), expected, os::core::ByteSpan{tampered.data(), sealed.value()}, opened));

    tampered = record;
    tampered[sealed.value() - 1U] ^= std::byte{0x01};
    assert(!crypto.open(
        key.value(), expected, os::core::ByteSpan{tampered.data(), sealed.value()}, opened));

    return 0;
}
