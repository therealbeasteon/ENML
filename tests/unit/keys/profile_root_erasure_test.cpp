#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/keys/error.hpp>
#include <os/keys/hierarchy.hpp>

namespace {

constexpr os::keys::KeyProtectionBinding system_binding{
    .scope = os::keys::KeyProtectionScope::system,
    .owner = {
        .principal = {0xA100000000000001ULL, 0xA200000000000001ULL},
        .user = os::core::UserId{0U},
    },
};
constexpr os::keys::KeyProtectionBinding profile_a{
    .scope = os::keys::KeyProtectionScope::user_profile,
    .owner = {
        .principal = {0xB100000000000001ULL, 0xB200000000000001ULL},
        .user = os::core::UserId{7U},
    },
};
constexpr os::keys::KeyProtectionBinding profile_b{
    .scope = os::keys::KeyProtectionScope::user_profile,
    .owner = {
        .principal = {0xB100000000000002ULL, 0xB200000000000002ULL},
        .user = os::core::UserId{8U},
    },
};
constexpr os::keys::KeyProtectionBinding app_a1{
    .scope = os::keys::KeyProtectionScope::application,
    .owner = {
        .principal = {0xC100000000000001ULL, 0xC200000000000001ULL},
        .user = os::core::UserId{7U},
    },
};
constexpr os::keys::KeyProtectionBinding app_a2{
    .scope = os::keys::KeyProtectionScope::application,
    .owner = {
        .principal = {0xC100000000000002ULL, 0xC200000000000002ULL},
        .user = os::core::UserId{7U},
    },
};
constexpr os::keys::KeyProtectionBinding app_b{
    .scope = os::keys::KeyProtectionScope::application,
    .owner = {
        .principal = {0xC100000000000003ULL, 0xC200000000000003ULL},
        .user = os::core::UserId{8U},
    },
};

class ErasureProvider final : public os::keys::HierarchicalKeyProvider {
public:
    os::core::Result<os::keys::ProviderKeyReference>
    generate(os::keys::KeyPurpose purpose) noexcept override {
        if (!os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::unsupported_purpose);
        }
        return os::keys::ProviderKeyReference{next_key_++};
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

    os::core::Result<void> destroy(os::keys::ProviderKeyReference key) noexcept override {
        if (!key.valid()) return os::keys::key_error(os::keys::errors::provider_failure);
        return {};
    }

    os::core::Result<std::size_t> persist_reference(
        os::keys::ProviderKeyReference,
        os::keys::KeyPurpose,
        os::core::ByteSpan,
        os::core::MutableByteSpan) noexcept override {
        return os::keys::key_error(os::keys::errors::provider_failure);
    }

    os::core::Result<os::keys::ProviderKeyReference> restore_reference(
        os::keys::KeyPurpose,
        os::core::ByteSpan,
        os::core::ByteSpan) noexcept override {
        return os::keys::key_error(os::keys::errors::provider_failure);
    }

    os::core::Result<os::keys::RootKeyReference> acquire_system_root(
        os::keys::KeyProtectionBinding binding) noexcept override {
        if (binding.scope != os::keys::KeyProtectionScope::system) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return os::keys::RootKeyReference{next_root_++};
    }

    os::core::Result<os::keys::RootKeyReference> acquire_child_root(
        os::keys::RootKeyReference parent,
        os::keys::KeyProtectionBinding parent_binding,
        os::keys::KeyProtectionBinding child_binding) noexcept override {
        if (!parent.valid() || !os::keys::valid_hierarchy_edge(parent_binding, child_binding)) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return os::keys::RootKeyReference{next_root_++};
    }

    os::core::Result<os::keys::ProviderKeyReference> generate_under_root(
        os::keys::RootKeyReference root,
        os::keys::KeyProtectionBinding,
        os::keys::KeyPurpose purpose) noexcept override {
        if (!root.valid() || !os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::provider_failure);
        }
        return os::keys::ProviderKeyReference{next_key_++};
    }

    os::core::Result<void> destroy_root(
        os::keys::RootKeyReference root,
        os::keys::KeyProtectionBinding binding) noexcept override {
        ++destroy_calls;
        if (!root.valid() || !binding.valid()) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        if (fail_on_destroy_call != 0U && destroy_calls == fail_on_destroy_call) {
            return os::keys::key_error(os::keys::errors::provider_failure);
        }
        destroyed_bindings[destroyed_count++] = binding;
        return {};
    }

    [[nodiscard]] os::keys::RootErasureAssurance root_erasure_assurance() const noexcept override {
        return assurance;
    }

    std::size_t destroy_calls {0U};
    std::size_t fail_on_destroy_call {0U};
    std::size_t destroyed_count {0U};
    os::keys::RootErasureAssurance assurance {os::keys::RootErasureAssurance::logical_only};
    os::keys::KeyProtectionBinding destroyed_bindings[8] {};

private:
    std::uint64_t next_root_ {1U};
    std::uint64_t next_key_ {1U};
};

void populate(os::keys::KeyHierarchy& hierarchy) {
    assert(hierarchy.initialize(system_binding));
    assert(hierarchy.ensure_profile(profile_a));
    assert(hierarchy.ensure_profile(profile_b));
    assert(hierarchy.ensure_application(app_a1));
    assert(hierarchy.ensure_application(app_a2));
    assert(hierarchy.ensure_application(app_b));
    assert(hierarchy.profile_count() == 2U);
    assert(hierarchy.application_count() == 3U);
}

} // namespace

int main() {
    // Software/unknown providers are conservative by default. Successful root
    // deletion alone is not enough to claim forensic erasure.
    {
        ErasureProvider provider;
        os::keys::KeyHierarchy hierarchy{provider};
        populate(hierarchy);

        const auto erased = hierarchy.destroy_profile(profile_a.owner.user);
        assert(erased);
        assert(erased.value().application_roots_destroyed == 2U);
        assert(erased.value().assurance == os::keys::RootErasureAssurance::logical_only);
        assert(hierarchy.profile_count() == 1U);
        assert(hierarchy.application_count() == 1U);

        // Both application roots are destroyed before the profile root. The
        // unrelated profile and application survive untouched.
        assert(provider.destroyed_count == 3U);
        assert(provider.destroyed_bindings[0].scope == os::keys::KeyProtectionScope::application);
        assert(provider.destroyed_bindings[1].scope == os::keys::KeyProtectionScope::application);
        assert(provider.destroyed_bindings[2] == profile_a);
        assert(provider.destroyed_bindings[0].owner.user == profile_a.owner.user);
        assert(provider.destroyed_bindings[1].owner.user == profile_a.owner.user);

        const auto missing = hierarchy.destroy_profile(profile_a.owner.user);
        assert(!missing);
        assert(missing.error() == os::keys::key_error(os::keys::errors::hierarchy_root_not_found));
    }

    // A partial provider failure is retryable without resurrecting a child that
    // was already destroyed. The profile root stays until every descendant is
    // gone, preventing an orphaned live child root.
    {
        ErasureProvider provider;
        os::keys::KeyHierarchy hierarchy{provider};
        populate(hierarchy);
        provider.fail_on_destroy_call = 2U;

        const auto first = hierarchy.destroy_profile(profile_a.owner.user);
        assert(!first);
        assert(first.error() == os::keys::key_error(os::keys::errors::provider_failure));
        assert(hierarchy.profile_count() == 2U);
        assert(hierarchy.application_count() == 2U);
        assert(provider.destroyed_count == 1U);

        provider.fail_on_destroy_call = 0U;
        const auto retry = hierarchy.destroy_profile(profile_a.owner.user);
        assert(retry);
        assert(retry.value().application_roots_destroyed == 1U);
        assert(hierarchy.profile_count() == 1U);
        assert(hierarchy.application_count() == 1U);
    }

    // A hardware-backed provider may explicitly report that its root storage is
    // effaceable. This still reports only provider-root assurance; whole-profile
    // forensic erasure additionally requires encrypted Storage coverage and
    // rollback-safe destruction state.
    {
        ErasureProvider provider;
        provider.assurance = os::keys::RootErasureAssurance::effaceable_key_storage;
        os::keys::KeyHierarchy hierarchy{provider};
        assert(hierarchy.initialize(system_binding));
        assert(hierarchy.ensure_profile(profile_a));

        const auto erased = hierarchy.destroy_profile(profile_a.owner.user);
        assert(erased);
        assert(erased.value().application_roots_destroyed == 0U);
        assert(erased.value().assurance ==
            os::keys::RootErasureAssurance::effaceable_key_storage);
    }

    return 0;
}
