#include <cassert>

#include <os/keys/hierarchy.hpp>
#include <os/keys/testing/openssl_provider.hpp>

namespace {

constexpr os::keys::KeyProtectionBinding system_binding{
    .scope = os::keys::KeyProtectionScope::system,
    .owner = os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0x7100000000000001ULL, 0x8100000000000001ULL},
        .user = os::core::UserId{0U},
    },
};

constexpr os::keys::KeyProtectionBinding profile_binding{
    .scope = os::keys::KeyProtectionScope::user_profile,
    .owner = os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0x7200000000000002ULL, 0x8200000000000002ULL},
        .user = os::core::UserId{42U},
    },
};

constexpr os::keys::KeyProtectionBinding application_binding{
    .scope = os::keys::KeyProtectionScope::application,
    .owner = os::keys::KeyOwner{
        .principal = os::core::PrincipalId{0x7300000000000003ULL, 0x8300000000000003ULL},
        .user = os::core::UserId{42U},
    },
};

} // namespace

int main() {
    os::keys::testing::OpenSslTestKeyProvider provider;
    os::keys::KeyHierarchy hierarchy{provider};

    assert(hierarchy.initialize(system_binding));
    assert(hierarchy.ensure_profile(profile_binding));

    auto storage_key = hierarchy.generate_profile_storage_key(os::core::UserId{42U});
    assert(storage_key);
    assert(storage_key.value().valid());

    auto missing_profile = hierarchy.generate_profile_storage_key(os::core::UserId{43U});
    assert(!missing_profile);

    auto invalid_user = hierarchy.generate_profile_storage_key(os::core::UserId{});
    assert(!invalid_user);

    assert(hierarchy.ensure_application(application_binding));
    auto app_key = hierarchy.generate_application_data_key(
        application_binding,
        os::keys::KeyPurpose::application_data_aead);
    assert(app_key);

    // Higher-level API refuses purpose confusion.
    auto confused = hierarchy.generate_application_data_key(
        application_binding,
        os::keys::KeyPurpose::profile_storage_aead);
    assert(!confused);

    // Provider boundary independently refuses the same confusion in both
    // directions, even if a caller bypasses KeyHierarchy policy.
    auto system_root = provider.acquire_system_root(system_binding);
    assert(system_root);
    auto profile_root = provider.acquire_child_root(
        system_root.value(), system_binding, profile_binding);
    assert(profile_root);
    auto app_root = provider.acquire_child_root(
        profile_root.value(), profile_binding, application_binding);
    assert(app_root);

    auto profile_as_app = provider.generate_under_root(
        profile_root.value(), profile_binding, os::keys::KeyPurpose::application_data_aead);
    assert(!profile_as_app);

    auto app_as_storage = provider.generate_under_root(
        app_root.value(), application_binding, os::keys::KeyPurpose::profile_storage_aead);
    assert(!app_as_storage);

    return 0;
}
