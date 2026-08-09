#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/keys/error.hpp>
#include <os/keys/policy.hpp>

namespace {

constexpr os::keys::KeyOwner owner_a{
    .principal = os::core::PrincipalId{0x8100000000000001ULL, 0x9100000000000001ULL},
    .user = os::core::UserId{7U},
};
constexpr os::keys::KeyOwner owner_a_wrong_user{
    .principal = owner_a.principal,
    .user = os::core::UserId{8U},
};
constexpr os::keys::KeyId key_id{0x504F4C4943594B31ULL, 1U};

class CountingStore final : public os::keys::KeyStore {
public:
    os::core::Result<os::keys::KeyDescriptor>
    create(
        os::keys::KeyOwner owner,
        os::keys::KeyId id,
        os::keys::KeyPurpose purpose,
        os::keys::RightsMask rights) noexcept override {
        ++create_calls;
        last_owner = owner;
        descriptor = os::keys::KeyDescriptor{
            .id = id,
            .version = 1U,
            .purpose = purpose,
            .rights = rights,
        };
        return descriptor;
    }

    os::core::Result<os::keys::KeyDescriptor>
    describe(os::keys::KeyOwner owner, os::keys::KeyId id) const noexcept override {
        ++describe_calls;
        if (owner != last_owner || id != descriptor.id) {
            return os::keys::key_error(os::keys::errors::not_found);
        }
        return descriptor;
    }

    os::core::Result<os::keys::KeyDescriptor>
    rotate(os::keys::KeyOwner owner, os::keys::KeyId id) noexcept override {
        ++rotate_calls;
        if (owner != last_owner || id != descriptor.id) {
            return os::keys::key_error(os::keys::errors::not_found);
        }
        ++descriptor.version;
        return descriptor;
    }

    os::core::Result<std::size_t>
    seal(
        os::keys::KeyOwner owner,
        os::keys::KeyId id,
        std::uint32_t,
        os::keys::CryptoProfileId,
        os::core::ByteSpan,
        os::core::ByteSpan,
        os::core::ByteSpan plaintext,
        os::core::MutableByteSpan ciphertext,
        os::keys::AeadNonce&,
        os::keys::AeadTag&) noexcept override {
        ++seal_calls;
        if (owner != last_owner || id != descriptor.id || ciphertext.size() < plaintext.size()) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return plaintext.size();
    }

    os::core::Result<std::size_t>
    open(
        os::keys::KeyOwner owner,
        os::keys::KeyId id,
        std::uint32_t,
        os::keys::CryptoProfileId,
        os::core::ByteSpan,
        os::core::ByteSpan,
        const os::keys::AeadNonce&,
        const os::keys::AeadTag&,
        os::core::ByteSpan ciphertext,
        os::core::MutableByteSpan plaintext) noexcept override {
        ++open_calls;
        if (owner != last_owner || id != descriptor.id || plaintext.size() < ciphertext.size()) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return ciphertext.size();
    }

    os::core::Result<void>
    destroy(os::keys::KeyOwner owner, os::keys::KeyId id) noexcept override {
        ++destroy_calls;
        if (owner != last_owner || id != descriptor.id) {
            return os::keys::key_error(os::keys::errors::not_found);
        }
        return {};
    }

    mutable std::size_t describe_calls {0U};
    std::size_t create_calls {0U};
    std::size_t rotate_calls {0U};
    std::size_t seal_calls {0U};
    std::size_t open_calls {0U};
    std::size_t destroy_calls {0U};
    os::keys::KeyOwner last_owner {};
    os::keys::KeyDescriptor descriptor {};
};

} // namespace

int main() {
    os::keys::ApplicationKeyPolicy policy;
    CountingStore backend;
    os::keys::PolicyKeyStore store{backend, policy};

    auto denied_create = store.create(
        owner_a,
        key_id,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(!denied_create);
    assert(denied_create.error() ==
        os::keys::key_error(os::keys::errors::policy_not_registered));
    assert(backend.create_calls == 0U);

    assert(policy.enable(owner_a));
    assert(policy.enable(owner_a));
    assert(policy.size() == 1U);
    assert(policy.enabled(owner_a));

    auto conflicting = policy.enable(owner_a_wrong_user);
    assert(!conflicting);
    assert(conflicting.error() ==
        os::keys::key_error(os::keys::errors::hierarchy_conflict));
    assert(policy.size() == 1U);

    auto created = store.create(
        owner_a,
        key_id,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(created);
    assert(backend.create_calls == 1U);

    auto described = store.describe(owner_a, key_id);
    assert(described);
    assert(backend.describe_calls == 1U);

    std::array<std::byte, 4U> input{};
    std::array<std::byte, 4U> output{};
    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    auto sealed = store.seal(
        owner_a,
        key_id,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {},
        {},
        input,
        output,
        nonce,
        tag);
    assert(sealed);
    assert(backend.seal_calls == 1U);

    assert(policy.disable(owner_a));
    assert(!policy.enabled(owner_a));
    assert(policy.size() == 0U);

    auto denied_existing = store.describe(owner_a, key_id);
    assert(!denied_existing);
    assert(denied_existing.error() ==
        os::keys::key_error(os::keys::errors::policy_not_registered));
    assert(backend.describe_calls == 1U);

    auto denied_rotate = store.rotate(owner_a, key_id);
    assert(!denied_rotate);
    assert(backend.rotate_calls == 0U);

    auto denied_destroy = store.destroy(owner_a, key_id);
    assert(!denied_destroy);
    assert(backend.destroy_calls == 0U);

    auto second_disable = policy.disable(owner_a);
    assert(!second_disable);
    assert(second_disable.error() ==
        os::keys::key_error(os::keys::errors::policy_not_registered));

    // Capacity is bounded and failure does not mutate existing entries.
    for (std::size_t index = 0U; index < os::keys::max_application_key_policies; ++index) {
        const os::keys::KeyOwner owner{
            .principal = os::core::PrincipalId{
                0x8200000000000000ULL,
                static_cast<std::uint64_t>(index + 1U),
            },
            .user = os::core::UserId{static_cast<std::uint64_t>(index + 100U)},
        };
        assert(policy.enable(owner));
    }
    assert(policy.size() == os::keys::max_application_key_policies);
    auto full = policy.enable(os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0x8300000000000001ULL, 1U},
        .user = os::core::UserId{999U},
    });
    assert(!full);
    assert(full.error() == os::keys::key_error(os::keys::errors::policy_capacity));

    return 0;
}
