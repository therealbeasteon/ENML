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
    std::size_t destroy_calls {0U};
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
    assert(provider.generate_calls == 1U);

    auto described = registry.describe(owner_a, key_one);
    assert(described);
    assert(described.value() == created.value());

    auto foreign = registry.describe(owner_b, key_one);
    assert(!foreign);
    assert(foreign.error() == os::keys::key_error(os::keys::errors::access_denied));

    auto provider_key = registry.provider_reference(
        owner_a,
        key_one,
        os::keys::key_rights::encrypt);
    assert(provider_key);
    assert(provider_key.value().valid());

    auto invalid_right = registry.provider_reference(owner_a, key_one, 0U);
    assert(!invalid_right);
    assert(invalid_right.error() == os::keys::key_error(os::keys::errors::invalid_rights));

    auto duplicate = registry.create(
        owner_a,
        key_one,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::all);
    assert(!duplicate);
    assert(duplicate.error() == os::keys::key_error(os::keys::errors::duplicate_key_id));
    assert(provider.generate_calls == 1U);

    auto metadata_only = registry.create(
        owner_a,
        key_two,
        os::keys::KeyPurpose::application_data_aead,
        os::keys::key_rights::metadata);
    assert(metadata_only);
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

    auto wrong_owner_destroy = registry.destroy(owner_b, key_one);
    assert(!wrong_owner_destroy);
    assert(wrong_owner_destroy.error() == os::keys::key_error(os::keys::errors::access_denied));

    auto destroyed = registry.destroy(owner_a, key_one);
    assert(destroyed);
    assert(provider.destroy_calls == 1U);
    assert(provider.last_destroyed == provider_key.value());
    assert(registry.record_count() == 2U);
    assert(registry.active_count() == 1U);

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

    // Tombstones consume registry identity space intentionally. Fill the
    // remaining slots with unique logical ids and verify capacity is checked
    // before asking the provider to generate another secret.
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
