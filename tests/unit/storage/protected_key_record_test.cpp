#include <array>
#include <cassert>
#include <cstddef>

#include <os/keys/hierarchy.hpp>
#include <os/keys/testing/openssl_provider.hpp>
#include <os/storage/protected_key_record.hpp>

namespace {

constexpr os::keys::KeyProtectionBinding system_binding{
    .scope = os::keys::KeyProtectionScope::system,
    .owner = os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0xB100000000000001ULL, 0xC100000000000001ULL},
        .user = os::core::UserId{0U},
    },
};
constexpr os::keys::KeyProtectionBinding profile_binding{
    .scope = os::keys::KeyProtectionScope::user_profile,
    .owner = os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0xB200000000000002ULL, 0xC200000000000002ULL},
        .user = os::core::UserId{42U},
    },
};
constexpr os::storage::ProtectedObjectKeyBinding object_binding{
    .principal = os::core::PrincipalId{0xB300000000000003ULL, 0xC300000000000003ULL},
    .user = os::core::UserId{42U},
    .object_id = os::storage::ProtectedObjectId{0x12345678U, 0xABCDEF01U},
    .generation = 1U,
};

} // namespace

int main() {
    std::array<std::byte, os::storage::max_protected_key_record_bytes> record{};
    std::size_t record_size = 0U;

    {
        os::keys::testing::OpenSslTestKeyProvider first_provider;
        os::keys::KeyHierarchy hierarchy{first_provider};
        assert(hierarchy.initialize(system_binding));
        assert(hierarchy.ensure_profile(profile_binding));
        auto key = hierarchy.generate_profile_storage_key(os::core::UserId{42U});
        assert(key);

        auto persisted = os::storage::persist_protected_object_key_v1(
            first_provider, key.value(), object_binding, record);
        assert(persisted);
        record_size = persisted.value();
    }

    // New provider instance models Key Service restart. Only the opaque wrapped
    // record crosses this boundary; the old in-memory ProviderKeyReference does not.
    os::keys::testing::OpenSslTestKeyProvider restarted_provider;
    auto restored = os::storage::restore_protected_object_key_v1(
        restarted_provider,
        object_binding,
        os::core::ByteSpan{record.data(), record_size});
    assert(restored);
    assert(restored.value().valid());

    auto wrong = object_binding;
    wrong.user = os::core::UserId{43U};
    assert(!os::storage::restore_protected_object_key_v1(
        restarted_provider, wrong, os::core::ByteSpan{record.data(), record_size}));

    wrong = object_binding;
    wrong.principal.low ^= 1U;
    assert(!os::storage::restore_protected_object_key_v1(
        restarted_provider, wrong, os::core::ByteSpan{record.data(), record_size}));

    wrong = object_binding;
    wrong.object_id.low ^= 1U;
    assert(!os::storage::restore_protected_object_key_v1(
        restarted_provider, wrong, os::core::ByteSpan{record.data(), record_size}));

    wrong = object_binding;
    wrong.generation += 1U;
    assert(!os::storage::restore_protected_object_key_v1(
        restarted_provider, wrong, os::core::ByteSpan{record.data(), record_size}));

    // Corrupting the opaque wrapped blob reaches the provider and must fail its
    // authenticated unwrap rather than installing attacker-controlled material.
    auto tampered = record;
    tampered[record_size - 1U] ^= std::byte{0x01};
    assert(!os::storage::restore_protected_object_key_v1(
        restarted_provider,
        object_binding,
        os::core::ByteSpan{tampered.data(), record_size}));

    return 0;
}
