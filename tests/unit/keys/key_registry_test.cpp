#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/keys/error.hpp>
#include <os/keys/key.hpp>
#include <os/keys/provider.hpp>
#include <os/keys/registry.hpp>

namespace {

class TestProvider final : public os::keys::KeyProvider {
public:
    os::core::Result<os::keys::ProviderKeyReference>
    generate(os::keys::KeyPurpose purpose) noexcept override {
        ++generate_calls;
        if (fail_generate || !os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::provider_failure);
        }
        return os::keys::ProviderKeyReference{next_reference++};
    }

    os::core::Result<std::size_t> seal(
        os::keys::ProviderKeyReference key,
        os::keys::CryptoProfileId profile,
        os::core::ByteSpan,
        os::core::ByteSpan,
        os::core::ByteSpan plaintext,
        os::core::MutableByteSpan ciphertext,
        os::keys::AeadNonce& nonce,
        os::keys::AeadTag& tag) noexcept override {
        ++seal_calls;
        last_seal_key = key;
        if (!key.valid() || profile != os::keys::CryptoProfileId::aes_256_gcm_v1) {
            return os::keys::key_error(os::keys::errors::provider_failure);
        }
        if (ciphertext.size() < plaintext.size()) {
            return os::keys::key_error(os::keys::errors::output_too_small);
        }
        for (std::size_t index = 0U; index < plaintext.size(); ++index) {
            ciphertext[index] = plaintext[index];
        }
        nonce.bytes.fill(std::byte{0x11});
        tag.bytes.fill(std::byte{0x22});
        return plaintext.size();
    }

    os::core::Result<std::size_t> open(
        os::keys::ProviderKeyReference key,
        os::keys::CryptoProfileId profile,
        os::core::ByteSpan,
        os::core::ByteSpan,
        const os::keys::AeadNonce&,
        const os::keys::AeadTag&,
        os::core::ByteSpan ciphertext,
        os::core::MutableByteSpan plaintext) noexcept override {
        ++open_calls;
        last_open_key = key;
        if (!key.valid() || profile != os::keys::CryptoProfileId::aes_256_gcm_v1) {
            return os::keys::key_error(os::keys::errors::provider_failure);
        }
        if (plaintext.size() < ciphertext.size()) {
            return os::keys::key_error(os::keys::errors::output_too_small);
        }
        for (std::size_t index = 0U; index < ciphertext.size(); ++index) {
            plaintext[index] = ciphertext[index];
        }
        return ciphertext.size();
    }

    os::core::Result<void>
    destroy(os::keys::ProviderKeyReference key) noexcept override {
        ++destroy_calls;
        last_destroyed = key;
        if (fail_destroy || !key.valid()) {
            return os::keys::key_error(os::keys::errors::provider_failure);
        }
        return {};
    }

    std::uint64_t next_reference {1U};
    std::size_t generate_calls {0U};
    std::size_t seal_calls {0U};
    std::size_t open_calls {0U};
    std::size_t destroy_calls {0U};
    os::keys::ProviderKeyReference last_seal_key {};
    os::keys::ProviderKeyReference last_open_key {};
    os::keys::ProviderKeyReference last_destroyed {};
    bool fail_generate {false};
    bool fail_destroy {false};
};

constexpr os::keys::KeyOwner owner_a{
    .principal = os::core::PrincipalId{0xA100000000000001ULL, 0xB100000000000001ULL},
    .user = os::core::UserId{7U},
};

constexpr os::keys::KeyOwner owner_b{
    .principal = os::core::PrincipalId{0xA200000000000002ULL, 0xB200000000000002ULL},
    .user = os::core::UserId{8U},
};

constexpr os::keys::KeyId key_one{0x4B45593100000000ULL, 1U};
constexpr os::keys::KeyId key_two{0x4B45593100000000ULL, 2U};
constexpr os::keys::KeyId key_three{0x4B45593100000000ULL, 3U};

} // namespace

int main() {
    TestProvider provider;
    os::keys::KeyRegistry registry{provider};

    auto created = registry.create(
        owner_a,
        key_one,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(created);
    assert(created.value().id == key_one);
    assert(created.value().version == 1U);
    assert(created.value().valid());
    assert(registry.record_count() == 1U);
    assert(registry.active_count() == 1U);
    assert(registry.version_count(key_one) == 1U);
    assert(provider.generate_calls == 1U);

    auto described = registry.describe(owner_a, key_one);
    assert(described);
    assert(described.value() == created.value());

    auto foreign = registry.describe(owner_b, key_one);
    assert(!foreign);
    assert(foreign.error() == os::keys::key_error(os::keys::errors::access_denied));

    auto provider_key_v1 = registry.provider_reference(
        owner_a,
        key_one,
        os::keys::key_rights::encrypt);
    assert(provider_key_v1);
    assert(provider_key_v1.value().valid());

    auto invalid_right = registry.provider_reference(owner_a, key_one, 0U);
    assert(!invalid_right);
    assert(invalid_right.error() == os::keys::key_error(os::keys::errors::invalid_rights));

    std::array<std::byte, 4U> plaintext{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    std::array<std::byte, 4U> ciphertext{};
    std::array<std::byte, 4U> reopened_plaintext{};
    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};

    auto wrong_owner_seal = registry.seal(
        owner_b,
        key_one,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, plaintext, ciphertext, nonce, tag);
    assert(!wrong_owner_seal);
    assert(wrong_owner_seal.error() == os::keys::key_error(os::keys::errors::access_denied));
    assert(provider.seal_calls == 0U);

    auto wrong_version_seal = registry.seal(
        owner_a,
        key_one,
        2U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, plaintext, ciphertext, nonce, tag);
    assert(!wrong_version_seal);
    assert(wrong_version_seal.error() ==
        os::keys::key_error(os::keys::errors::key_version_mismatch));
    assert(provider.seal_calls == 0U);

    auto valid_seal = registry.seal(
        owner_a,
        key_one,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, plaintext, ciphertext, nonce, tag);
    assert(valid_seal);
    assert(valid_seal.value() == plaintext.size());
    assert(provider.seal_calls == 1U);
    assert(provider.last_seal_key == provider_key_v1.value());

    auto valid_open = registry.open(
        owner_a,
        key_one,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, nonce, tag, ciphertext, reopened_plaintext);
    assert(valid_open);
    assert(valid_open.value() == plaintext.size());
    assert(reopened_plaintext == plaintext);
    assert(provider.open_calls == 1U);
    assert(provider.last_open_key == provider_key_v1.value());

    const auto generate_before_wrong_rotate = provider.generate_calls;
    auto foreign_rotate = registry.rotate(owner_b, key_one);
    assert(!foreign_rotate);
    assert(foreign_rotate.error() == os::keys::key_error(os::keys::errors::access_denied));
    assert(provider.generate_calls == generate_before_wrong_rotate);

    auto rotated = registry.rotate(owner_a, key_one);
    assert(rotated);
    assert(rotated.value().version == 2U);
    assert(registry.version_count(key_one) == 2U);

    auto provider_key_v2 = registry.provider_reference(
        owner_a,
        key_one,
        os::keys::key_rights::encrypt);
    assert(provider_key_v2);
    assert(provider_key_v2.value().valid());
    assert(provider_key_v2.value() != provider_key_v1.value());

    const auto seal_calls_before_old = provider.seal_calls;
    auto old_version_seal = registry.seal(
        owner_a,
        key_one,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, plaintext, ciphertext, nonce, tag);
    assert(!old_version_seal);
    assert(old_version_seal.error() ==
        os::keys::key_error(os::keys::errors::key_version_mismatch));
    assert(provider.seal_calls == seal_calls_before_old);

    auto current_seal = registry.seal(
        owner_a,
        key_one,
        2U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, plaintext, ciphertext, nonce, tag);
    assert(current_seal);
    assert(provider.last_seal_key == provider_key_v2.value());

    auto historical_open = registry.open(
        owner_a,
        key_one,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, nonce, tag, ciphertext, reopened_plaintext);
    assert(historical_open);
    assert(provider.last_open_key == provider_key_v1.value());

    auto current_open = registry.open(
        owner_a,
        key_one,
        2U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, nonce, tag, ciphertext, reopened_plaintext);
    assert(current_open);
    assert(provider.last_open_key == provider_key_v2.value());

    const auto generate_before_duplicate = provider.generate_calls;
    auto duplicate = registry.create(
        owner_a,
        key_one,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(!duplicate);
    assert(duplicate.error() == os::keys::key_error(os::keys::errors::duplicate_key_id));
    assert(provider.generate_calls == generate_before_duplicate);

    auto metadata_only = registry.create(
        owner_a,
        key_two,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::metadata);
    assert(metadata_only);

    const auto seal_calls_before_denied = provider.seal_calls;
    auto denied_seal = registry.seal(
        owner_a,
        key_two,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, plaintext, ciphertext, nonce, tag);
    assert(!denied_seal);
    assert(denied_seal.error() == os::keys::key_error(os::keys::errors::access_denied));
    assert(provider.seal_calls == seal_calls_before_denied);

    const auto generate_before_denied_rotate = provider.generate_calls;
    auto denied_rotate = registry.rotate(owner_a, key_two);
    assert(!denied_rotate);
    assert(denied_rotate.error() == os::keys::key_error(os::keys::errors::access_denied));
    assert(provider.generate_calls == generate_before_denied_rotate);

    auto denied_destroy = registry.destroy(owner_a, key_two);
    assert(!denied_destroy);
    assert(denied_destroy.error() == os::keys::key_error(os::keys::errors::access_denied));
    assert(provider.destroy_calls == 0U);

    provider.fail_generate = true;
    const auto records_before_failure = registry.record_count();
    auto failed_create = registry.create(
        owner_a,
        key_three,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(!failed_create);
    assert(failed_create.error() == os::keys::key_error(os::keys::errors::provider_failure));
    assert(registry.record_count() == records_before_failure);
    provider.fail_generate = false;

    auto version_limited = registry.create(
        owner_a,
        key_three,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(version_limited);
    for (std::size_t index = 1U; index < os::keys::max_key_versions; ++index) {
        auto next = registry.rotate(owner_a, key_three);
        assert(next);
        assert(next.value().version == static_cast<std::uint32_t>(index + 1U));
    }
    assert(registry.version_count(key_three) == os::keys::max_key_versions);
    const auto generate_before_limit = provider.generate_calls;
    auto over_limit = registry.rotate(owner_a, key_three);
    assert(!over_limit);
    assert(over_limit.error() == os::keys::key_error(os::keys::errors::version_limit));
    assert(provider.generate_calls == generate_before_limit);

    auto wrong_owner_destroy = registry.destroy(owner_b, key_one);
    assert(!wrong_owner_destroy);
    assert(wrong_owner_destroy.error() == os::keys::key_error(os::keys::errors::access_denied));

    auto destroyed = registry.destroy(owner_a, key_one);
    assert(destroyed);
    assert(provider.destroy_calls == 2U);
    assert(provider.last_destroyed == provider_key_v2.value());
    assert(registry.record_count() == 3U);
    assert(registry.active_count() == 2U);

    const auto open_calls_before_destroyed = provider.open_calls;
    auto destroyed_open = registry.open(
        owner_a,
        key_one,
        1U,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {}, {}, nonce, tag, ciphertext, reopened_plaintext);
    assert(!destroyed_open);
    assert(destroyed_open.error() == os::keys::key_error(os::keys::errors::destroyed));
    assert(provider.open_calls == open_calls_before_destroyed);

    auto after_destroy = registry.describe(owner_a, key_one);
    assert(!after_destroy);
    assert(after_destroy.error() == os::keys::key_error(os::keys::errors::destroyed));

    auto reuse_destroyed_id = registry.create(
        owner_a,
        key_one,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(!reuse_destroyed_id);
    assert(reuse_destroyed_id.error() == os::keys::key_error(os::keys::errors::duplicate_key_id));

    std::uint64_t next_id = 100U;
    while (registry.record_count() < os::keys::max_key_records) {
        const os::keys::KeyId id{0x4B45593200000000ULL, next_id++};
        auto result = registry.create(
            owner_a,
            id,
            os::keys::KeyPurpose::application_data_aead,
            os::keys::key_rights::all);
        assert(result);
    }
    const auto generate_before_full = provider.generate_calls;
    auto full = registry.create(
        owner_a,
        os::keys::KeyId{0x4B45593300000000ULL, 1U},
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(!full);
    assert(full.error() == os::keys::key_error(os::keys::errors::registry_full));
    assert(provider.generate_calls == generate_before_full);

    return 0;
}
