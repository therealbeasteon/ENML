#include <array>
#include <cassert>
#include <cstddef>

#include <os/keys/hierarchy.hpp>
#include <os/keys/testing/openssl_provider.hpp>
#include <os/storage/protected_namespace_snapshot.hpp>

namespace {

class IdSource final : public os::storage::ProtectedObjectIdSource {
public:
    explicit IdSource(std::uint64_t seed) noexcept : next_(seed) {}

    os::core::Result<os::storage::ProtectedObjectId> next() noexcept override {
        return os::storage::ProtectedObjectId{0xC00C1EULL, next_++};
    }

private:
    std::uint64_t next_ {1U};
};

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
    auto metadata_key = hierarchy.generate_profile_storage_metadata_key(os::core::UserId{42U});
    assert(metadata_key);

    IdSource source_ids{1U};
    os::storage::ProtectedNamespaceRegistry source{source_ids};
    auto first_path = os::storage::RelativePath::parse("notes/private.txt");
    auto second_path = os::storage::RelativePath::parse("vault/index.db");
    assert(first_path && second_path);
    auto first = source.create(profile_binding.owner.principal, os::core::UserId{42U}, first_path.value());
    auto second = source.create(profile_binding.owner.principal, os::core::UserId{42U}, second_path.value());
    assert(first && second);
    assert(source.publish_generation(
        profile_binding.owner.principal,
        os::core::UserId{42U},
        first_path.value(),
        1U,
        3U));

    const os::storage::ProtectedNamespaceSnapshotHeaderV1 header{
        .user = os::core::UserId{42U},
        .security_epoch = os::keys::SecurityEpoch{9U},
        .sequence = 7U,
        .entry_count = 2U,
        .flags = 0U,
    };
    os::storage::ProtectedNamespaceSnapshotCrypto crypto{provider};
    std::array<std::byte, os::storage::max_protected_namespace_snapshot_plaintext_bytes> scratch{};
    std::array<std::byte, os::storage::max_protected_namespace_snapshot_record_bytes> record{};
    auto sealed = crypto.seal(metadata_key.value(), header, source, scratch, record);
    assert(sealed);

    IdSource target_ids{100U};
    os::storage::ProtectedNamespaceRegistry target{target_ids};
    auto sentinel_path = os::storage::RelativePath::parse("old/state");
    assert(sentinel_path);
    assert(target.create(
        profile_binding.owner.principal,
        os::core::UserId{42U},
        sentinel_path.value()));

    const os::storage::ProtectedNamespaceFreshnessEvidence freshness{
        .user = os::core::UserId{42U},
        .current_security_epoch = os::keys::SecurityEpoch{9U},
        .minimum_sequence = 7U,
    };

    // Tamper must fail authentication and leave the live registry unchanged.
    auto tampered = record;
    tampered[sealed.value() - 1U] ^= std::byte{0x01};
    auto failed = crypto.open_and_restore(
        metadata_key.value(),
        freshness,
        os::core::ByteSpan{tampered.data(), sealed.value()},
        scratch,
        target);
    assert(!failed);
    assert(target.find(
        profile_binding.owner.principal,
        os::core::UserId{42U},
        sentinel_path.value()) != nullptr);

    auto restored = crypto.open_and_restore(
        metadata_key.value(),
        freshness,
        os::core::ByteSpan{record.data(), sealed.value()},
        scratch,
        target);
    assert(restored);
    assert(target.size() == 2U);
    const auto* restored_first = target.find(
        profile_binding.owner.principal,
        os::core::UserId{42U},
        first_path.value());
    const auto* restored_second = target.find(
        profile_binding.owner.principal,
        os::core::UserId{42U},
        second_path.value());
    assert(restored_first != nullptr && restored_second != nullptr);
    assert(restored_first->object_id == first.value().object_id);
    assert(restored_first->generation == 3U);
    assert(restored_second->object_id == second.value().object_id);
    assert(target.find(
        profile_binding.owner.principal,
        os::core::UserId{42U},
        sentinel_path.value()) == nullptr);

    // A cryptographically valid snapshot is still refused when independent
    // trusted freshness has moved forward.
    auto newer_freshness = freshness;
    newer_freshness.minimum_sequence = 8U;
    assert(!crypto.open_and_restore(
        metadata_key.value(),
        newer_freshness,
        os::core::ByteSpan{record.data(), sealed.value()},
        scratch,
        target));

    return 0;
}
