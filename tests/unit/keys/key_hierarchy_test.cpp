#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/keys/error.hpp>
#include <os/keys/hierarchy.hpp>

namespace {

constexpr os::keys::KeyOwner system_owner{
    .principal = os::core::PrincipalId{0x5100000000000001ULL, 0x6100000000000001ULL},
    .user = os::core::UserId{0U},
};
constexpr os::keys::KeyOwner profile_owner{
    .principal = os::core::PrincipalId{0x5200000000000002ULL, 0x6200000000000002ULL},
    .user = os::core::UserId{42U},
};
constexpr os::keys::KeyOwner application_owner{
    .principal = os::core::PrincipalId{0x5300000000000003ULL, 0x6300000000000003ULL},
    .user = os::core::UserId{42U},
};
constexpr os::keys::KeyOwner foreign_application_owner{
    .principal = os::core::PrincipalId{0x5400000000000004ULL, 0x6400000000000004ULL},
    .user = os::core::UserId{43U},
};

constexpr os::keys::KeyProtectionBinding system_binding{
    .scope = os::keys::KeyProtectionScope::system,
    .owner = system_owner,
};
constexpr os::keys::KeyProtectionBinding profile_binding{
    .scope = os::keys::KeyProtectionScope::user_profile,
    .owner = profile_owner,
};
constexpr os::keys::KeyProtectionBinding application_binding{
    .scope = os::keys::KeyProtectionScope::application,
    .owner = application_owner,
};
constexpr os::keys::KeyProtectionBinding foreign_application_binding{
    .scope = os::keys::KeyProtectionScope::application,
    .owner = foreign_application_owner,
};

class TestHierarchicalProvider final : public os::keys::HierarchicalKeyProvider {
public:
    os::core::Result<os::keys::ProviderKeyReference>
    generate(os::keys::KeyPurpose purpose) noexcept override {
        if (!os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::unsupported_purpose);
        }
        return os::keys::ProviderKeyReference{next_provider_reference_++};
    }

    os::core::Result<std::size_t> seal(
        os::keys::ProviderKeyReference,
        os::keys::CryptoProfileId,
        os::core::ByteSpan,
        os::core::ByteSpan,
        os::core::ByteSpan,
        os::core::MutableByteSpan,
        os::keys::AeadNonce&,
        os::keys::AeadTag&) noexcept override {
        return os::keys::key_error(os::keys::errors::provider_failure);
    }

    os::core::Result<std::size_t> open(
        os::keys::ProviderKeyReference,
        os::keys::CryptoProfileId,
        os::core::ByteSpan,
        os::core::ByteSpan,
        const os::keys::AeadNonce&,
        const os::keys::AeadTag&,
        os::core::ByteSpan,
        os::core::MutableByteSpan) noexcept override {
        return os::keys::key_error(os::keys::errors::provider_failure);
    }

    os::core::Result<void>
    destroy(os::keys::ProviderKeyReference key) noexcept override {
        if (!key.valid()) return os::keys::key_error(os::keys::errors::provider_failure);
        return {};
    }

    os::core::Result<std::size_t>
    persist_reference(
        os::keys::ProviderKeyReference,
        os::keys::KeyPurpose,
        os::core::ByteSpan,
        os::core::MutableByteSpan) noexcept override {
        return os::keys::key_error(os::keys::errors::provider_failure);
    }

    os::core::Result<os::keys::ProviderKeyReference>
    restore_reference(
        os::keys::KeyPurpose,
        os::core::ByteSpan,
        os::core::ByteSpan) noexcept override {
        return os::keys::key_error(os::keys::errors::provider_failure);
    }

    os::core::Result<os::keys::RootKeyReference>
    acquire_system_root(os::keys::KeyProtectionBinding binding) noexcept override {
        if (!binding.valid() || binding.scope != os::keys::KeyProtectionScope::system) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return os::keys::RootKeyReference{next_root_reference_++};
    }

    os::core::Result<os::keys::RootKeyReference>
    create_child_root(
        os::keys::RootKeyReference parent,
        os::keys::KeyProtectionBinding parent_binding,
        os::keys::KeyProtectionBinding child_binding) noexcept override {
        if (!parent.valid() || !os::keys::valid_hierarchy_edge(parent_binding, child_binding)) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return os::keys::RootKeyReference{next_root_reference_++};
    }

    os::core::Result<os::keys::ProviderKeyReference>
    generate_under_root(
        os::keys::RootKeyReference root,
        os::keys::KeyProtectionBinding binding,
        os::keys::KeyPurpose purpose) noexcept override {
        if (!root.valid() || !binding.valid() || !os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return os::keys::ProviderKeyReference{next_provider_reference_++};
    }

    os::core::Result<void>
    destroy_root(
        os::keys::RootKeyReference root,
        os::keys::KeyProtectionBinding binding) noexcept override {
        if (!root.valid() || !binding.valid()) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return {};
    }

private:
    std::uint64_t next_root_reference_ {1U};
    std::uint64_t next_provider_reference_ {1U};
};

class TestMonotonicSecurityState final : public os::keys::MonotonicSecurityState {
public:
    os::core::Result<os::keys::SecurityEpoch> current() noexcept override {
        return current_;
    }

    os::core::Result<os::keys::SecurityEpoch>
    advance(os::keys::SecurityEpoch expected_current) noexcept override {
        if (!expected_current.valid() || expected_current != current_) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        if (!current_.can_advance()) {
            return os::keys::key_error(os::keys::errors::version_limit);
        }
        current_.value += 1U;
        return current_;
    }

private:
    os::keys::SecurityEpoch current_{1U};
};

} // namespace

int main() {
    static_assert(system_binding.valid());
    static_assert(profile_binding.valid());
    static_assert(application_binding.valid());
    static_assert(os::keys::valid_hierarchy_edge(system_binding, profile_binding));
    static_assert(os::keys::valid_hierarchy_edge(profile_binding, application_binding));
    static_assert(!os::keys::valid_hierarchy_edge(profile_binding, foreign_application_binding));
    static_assert(!os::keys::valid_hierarchy_edge(system_binding, application_binding));
    static_assert(!os::keys::valid_hierarchy_edge(application_binding, profile_binding));
    static_assert(!os::keys::valid_hierarchy_edge(profile_binding, system_binding));

    constexpr os::keys::KeyProtectionBinding invalid_binding{
        .scope = static_cast<os::keys::KeyProtectionScope>(99U),
        .owner = application_owner,
    };
    static_assert(!invalid_binding.valid());
    static_assert(!os::keys::valid_hierarchy_edge(profile_binding, invalid_binding));

    constexpr os::keys::SecurityEpoch zero_epoch{};
    constexpr os::keys::SecurityEpoch first_epoch{1U};
    constexpr os::keys::SecurityEpoch maximum_epoch{std::numeric_limits<std::uint64_t>::max()};
    static_assert(!zero_epoch.valid());
    static_assert(first_epoch.valid());
    static_assert(first_epoch.can_advance());
    static_assert(!maximum_epoch.can_advance());

    TestHierarchicalProvider provider;
    auto system_root = provider.acquire_system_root(system_binding);
    assert(system_root);
    assert(system_root.value().valid());

    auto rejected_application_root = provider.acquire_system_root(application_binding);
    assert(!rejected_application_root);

    auto profile_root = provider.create_child_root(
        system_root.value(), system_binding, profile_binding);
    assert(profile_root);

    auto application_root = provider.create_child_root(
        profile_root.value(), profile_binding, application_binding);
    assert(application_root);

    auto cross_user_root = provider.create_child_root(
        profile_root.value(), profile_binding, foreign_application_binding);
    assert(!cross_user_root);

    auto upward_root = provider.create_child_root(
        application_root.value(), application_binding, profile_binding);
    assert(!upward_root);

    auto data_key = provider.generate_under_root(
        application_root.value(),
        application_binding,
        os::keys::KeyPurpose::application_data_aead);
    assert(data_key);
    assert(data_key.value().valid());

    TestMonotonicSecurityState security_state;
    auto epoch = security_state.current();
    assert(epoch);
    assert(epoch.value().value == 1U);

    auto advanced = security_state.advance(epoch.value());
    assert(advanced);
    assert(advanced.value().value == 2U);

    auto stale_advance = security_state.advance(epoch.value());
    assert(!stale_advance);
    auto still_current = security_state.current();
    assert(still_current);
    assert(still_current.value().value == 2U);

    assert(provider.destroy_root(application_root.value(), application_binding));
    assert(provider.destroy_root(profile_root.value(), profile_binding));
    assert(provider.destroy_root(system_root.value(), system_binding));
    return 0;
}
