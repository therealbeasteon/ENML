#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/identity.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/keys/error.hpp>
#include <os/keys/id_source.hpp>
#include <os/keys/key.hpp>
#include <os/keys/provider.hpp>
#include <os/keys/registry.hpp>
#include <os/keys/service.hpp>

namespace {

constexpr os::core::PeerIdentity owner_a_identity{
    .principal = os::core::PrincipalId{0xAA04000000000001ULL, 0xBB04000000000001ULL},
    .user = os::core::UserId{40U},
    .process = os::core::ProcessId{401U},
};

constexpr os::core::PeerIdentity owner_b_identity{
    .principal = os::core::PrincipalId{0xAA04000000000002ULL, 0xBB04000000000002ULL},
    .user = os::core::UserId{41U},
    .process = os::core::ProcessId{402U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    explicit TestIdentityResolver(pid_t owner_a_pid) noexcept : owner_a_pid_(owner_a_pid) {}

    os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id <= 0 ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid())) {
            return os::core::make_error(os::core::ErrorDomain::security, 1U);
        }
        if (credentials.process_id == static_cast<std::int64_t>(owner_a_pid_)) {
            return owner_a_identity;
        }
        return owner_b_identity;
    }

private:
    pid_t owner_a_pid_ {-1};
};

class TestProvider final : public os::keys::KeyProvider {
public:
    os::core::Result<os::keys::ProviderKeyReference>
    generate(os::keys::KeyPurpose purpose) noexcept override {
        if (!os::keys::valid_purpose(purpose)) {
            return os::keys::key_error(os::keys::errors::unsupported_purpose);
        }
        return os::keys::ProviderKeyReference{next_reference_++};
    }

    os::core::Result<void>
    destroy(os::keys::ProviderKeyReference key) noexcept override {
        if (!key.valid()) return os::keys::key_error(os::keys::errors::provider_failure);
        return {};
    }

private:
    std::uint64_t next_reference_ {1U};
};

class TestIdSource final : public os::keys::KeyIdSource {
public:
    os::core::Result<os::keys::KeyId> next() noexcept override {
        return os::keys::KeyId{0x4B45595445535431ULL, next_++};
    }

private:
    std::uint64_t next_ {1U};
};

[[noreturn]] void run_server(os::ipc::Channel channel, pid_t owner_a_pid) {
    TestIdentityResolver resolver{owner_a_pid};
    TestProvider provider;
    TestIdSource ids;
    os::keys::KeyRegistry registry{provider};
    os::keys::KeyService service{channel, resolver, registry, ids};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    for (;;) {
        auto result = service.dispatch_once(scratch, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                std::_Exit(0);
            }
            std::_Exit(20);
        }
    }
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();

    const pid_t parent_pid = ::getpid();
    const pid_t server = ::fork();
    assert(server >= 0);
    if (server == 0) {
        channels[0].close();
        run_server(std::move(channels[1]), parent_pid);
    }

    channels[1].close();
    os::ipc::ClientConnection connection{channels[0]};
    os::keys::KeyClient keys{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto created_result = keys.create_application_data_key(scratch);
    assert(created_result);
    auto created = std::move(created_result).value();
    assert(created.valid());
    assert(created.descriptor().version == 1U);
    assert(created.descriptor().purpose == os::keys::KeyPurpose::application_data_aead);
    assert(created.descriptor().rights == os::keys::key_rights::all);
    const auto key_id = created.descriptor().id;

    auto duplicate_result = keys.open(key_id, scratch);
    assert(duplicate_result);
    auto duplicate = std::move(duplicate_result).value();
    assert(duplicate.descriptor().id == key_id);

    // A forked process inherits the same transport descriptor, but per-message
    // SCM_CREDENTIALS changes the trusted caller identity. Knowing the public
    // KeyId and possessing the main transport is therefore insufficient to
    // open another principal's key.
    const pid_t attacker = ::fork();
    assert(attacker >= 0);
    if (attacker == 0) {
        auto stolen = keys.open(key_id, scratch);
        if (stolen) std::_Exit(30);
        if (stolen.error().domain != os::core::ErrorDomain::key ||
            stolen.error().code != os::keys::errors::access_denied) {
            std::_Exit(31);
        }
        std::_Exit(0);
    }
    int attacker_status = 0;
    assert(::waitpid(attacker, &attacker_status, 0) == attacker);
    assert(WIFEXITED(attacker_status));
    assert(WEXITSTATUS(attacker_status) == 0);

    assert(created.destroy(scratch));
    assert(!created.valid());

    // Destroy is key-wide revocation. A second already-minted bearer endpoint
    // for the same key is closed by the service rather than remaining usable.
    auto stale_destroy = duplicate.destroy(scratch);
    assert(!stale_destroy);
    assert(stale_destroy.error().domain == os::core::ErrorDomain::ipc);
    assert(stale_destroy.error().code == os::ipc::errors::peer_died);

    auto destroyed_open = keys.open(key_id, scratch);
    assert(!destroyed_open);
    assert(destroyed_open.error().domain == os::core::ErrorDomain::key);
    assert(destroyed_open.error().code == os::keys::errors::destroyed);

    // The service remains alive and can create a fresh logical key after
    // revoking the previous one.
    auto second = keys.create_application_data_key(scratch);
    assert(second);
    assert(second.value().descriptor().id != key_id);

    channels[0].close();
    int server_status = 0;
    assert(::waitpid(server, &server_status, 0) == server);
    assert(WIFEXITED(server_status));
    assert(WEXITSTATUS(server_status) == 0);
    return 0;
}
