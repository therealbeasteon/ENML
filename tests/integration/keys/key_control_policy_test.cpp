#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/keys/control.hpp>
#include <os/keys/error.hpp>
#include <os/keys/hierarchy.hpp>
#include <os/keys/id_source.hpp>
#include <os/keys/policy.hpp>
#include <os/keys/registry.hpp>
#include <os/keys/service.hpp>
#include <os/service/identity.hpp>

namespace {

constexpr os::core::PrincipalId system_principal{
    0x8A00000000000001ULL,
    0x9A00000000000001ULL,
};
constexpr os::core::PrincipalId application_principal{
    0x8B00000000000002ULL,
    0x9B00000000000002ULL,
};
constexpr os::core::UserId application_user{77U};

class TestProvider final : public os::keys::HierarchicalKeyProvider {
public:
    os::core::Result<os::keys::ProviderKeyReference>
    generate(os::keys::KeyPurpose purpose) noexcept override {
        if (!os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::unsupported_purpose);
        }
        return os::keys::ProviderKeyReference{next_provider_++};
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
        return os::keys::RootKeyReference{next_root_++};
    }

    os::core::Result<os::keys::RootKeyReference>
    acquire_child_root(
        os::keys::RootKeyReference parent,
        os::keys::KeyProtectionBinding parent_binding,
        os::keys::KeyProtectionBinding child_binding) noexcept override {
        if (!parent.valid() || !os::keys::valid_hierarchy_edge(parent_binding, child_binding)) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return os::keys::RootKeyReference{next_root_++};
    }

    os::core::Result<os::keys::ProviderKeyReference>
    generate_under_root(
        os::keys::RootKeyReference root,
        os::keys::KeyProtectionBinding binding,
        os::keys::KeyPurpose purpose) noexcept override {
        if (!root.valid() || !binding.valid() ||
            binding.scope != os::keys::KeyProtectionScope::application ||
            !os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::access_denied);
        }
        return os::keys::ProviderKeyReference{next_provider_++};
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
    std::uint64_t next_root_ {1U};
    std::uint64_t next_provider_ {1U};
};

class TestIdSource final : public os::keys::KeyIdSource {
public:
    os::core::Result<os::keys::KeyId> next() noexcept override {
        return os::keys::KeyId{0x434F4E54524F4C31ULL, next_++};
    }
private:
    std::uint64_t next_ {1U};
};

[[noreturn]] void run_router(os::ipc::Channel control) {
    TestProvider provider;
    os::keys::KeyHierarchy hierarchy{provider};
    const os::keys::KeyProtectionBinding system_binding{
        .scope = os::keys::KeyProtectionScope::system,
        .owner = os::keys::KeyOwner{
            .principal = system_principal,
            .user = os::core::UserId{0U},
        },
    };
    if (!hierarchy.initialize(system_binding)) std::_Exit(20);

    os::keys::ApplicationKeyPolicy policy;
    os::keys::KeyRegistry raw_store{provider};
    os::keys::PolicyKeyStore gated_store{raw_store, policy};
    os::service::IdentityRegistry identities;
    TestIdSource ids;

    auto public_pair = os::ipc::Channel::create_local_pair();
    if (!public_pair) std::_Exit(21);
    auto public_channels = std::move(public_pair).value();
    os::keys::KeyService service{public_channels[0], identities, gated_store, ids};
    os::keys::KeyControlRouter router{policy, hierarchy, service, identities};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    for (std::size_t index = 0U; index < 5U; ++index) {
        auto handled = router.dispatch_once(control, scratch);
        if (!handled) std::_Exit(22);
    }

    const os::keys::KeyOwner owner{
        .principal = application_principal,
        .user = application_user,
    };
    if (hierarchy.profile_count() != 2U || hierarchy.application_count() != 1U ||
        policy.size() != 0U || policy.enabled(owner)) {
        std::_Exit(23);
    }
    std::_Exit(0);
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        channels[0].close();
        run_router(std::move(channels[1]));
    }

    channels[1].close();
    os::keys::KeyControlClient control{channels[0]};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    assert(control.ensure_profile(application_user, scratch, 1000U));
    assert(control.enable_application(application_principal, application_user, scratch, 1000U));
    // Exact trusted replay is idempotent.
    assert(control.enable_application(application_principal, application_user, scratch, 1000U));

    // The same durable PrincipalId cannot be silently rebound to another user.
    auto rebind = control.enable_application(
        application_principal,
        os::core::UserId{78U},
        scratch,
        1000U);
    assert(!rebind);
    assert(rebind.error() == os::keys::key_error(os::keys::errors::hierarchy_conflict));

    assert(control.disable_application(application_principal, application_user, scratch, 1000U));

    channels[0].close();
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
