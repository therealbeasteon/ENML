#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/id_source.hpp>
#include <os/keys/persistence.hpp>
#include <os/keys/service.hpp>
#include <os/keys/testing/openssl_provider.hpp>

namespace {

constexpr os::core::PeerIdentity owner_identity{
    .principal = os::core::PrincipalId{0xAA08000000000001ULL, 0xBB08000000000001ULL},
    .user = os::core::UserId{0x100000058ULL},
    .process = os::core::ProcessId{801U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    explicit TestIdentityResolver(pid_t owner_pid) noexcept : owner_pid_(owner_pid) {}

    os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id != static_cast<std::int64_t>(owner_pid_) ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid())) {
            return os::core::make_error(
                os::core::ErrorDomain::security,
                os::core::errors::security::credential_mismatch);
        }
        return owner_identity;
    }

private:
    pid_t owner_pid_ {-1};
};

class TestIdSource final : public os::keys::KeyIdSource {
public:
    os::core::Result<os::keys::KeyId> next() noexcept override {
        return os::keys::KeyId{0x5253544152544B59ULL, next_++};
    }

private:
    std::uint64_t next_ {1U};
};

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

[[noreturn]] void run_server(
    os::ipc::Channel channel,
    pid_t owner_pid,
    const char* state_directory) {
    const int state_fd = ::open(state_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (state_fd < 0) std::_Exit(30);

    TestIdentityResolver resolver{owner_pid};
    os::keys::testing::OpenSslTestKeyProvider provider;
    auto persistent_result = os::keys::PersistentKeyRegistry::open(
        os::core::NativeHandle(state_fd), provider);
    if (!persistent_result) std::_Exit(31);
    auto persistent = std::move(persistent_result).value();
    TestIdSource ids;
    os::keys::KeyService service{channel, resolver, persistent, ids};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    for (;;) {
        auto result = service.dispatch_once(scratch, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                std::_Exit(0);
            }
            std::_Exit(32);
        }
    }
}

struct ServerProcess final {
    pid_t pid {-1};
    os::ipc::Channel channel {};
};

[[nodiscard]] ServerProcess spawn_server(const char* state_directory) {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();
    const pid_t owner_pid = ::getpid();
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        channels[0].close();
        run_server(std::move(channels[1]), owner_pid, state_directory);
    }
    channels[1].close();
    return ServerProcess{child, std::move(channels[0])};
}

void stop_server(ServerProcess& server) {
    server.channel.close();
    int status = 0;
    assert(::waitpid(server.pid, &status, 0) == server.pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    server.pid = -1;
}

void assert_plaintext(
    os::keys::KeyObjectHandle& key,
    os::core::ByteSpan envelope,
    std::string_view expected,
    os::core::MutableByteSpan scratch) {
    std::array<std::byte, os::keys::max_key_plaintext_bytes> output{};
    auto decrypted = key.decrypt(
        envelope,
        as_bytes("restart-bound-aad"),
        output,
        scratch);
    assert(decrypted);
    assert(decrypted.value() == expected.size());
    assert(std::equal(
        output.begin(),
        output.begin() + static_cast<std::ptrdiff_t>(decrypted.value()),
        as_bytes(expected).begin()));
}

} // namespace

int main() {
    char directory_template[] = "/tmp/enml-key-service-restart-XXXXXX";
    char* state_directory = ::mkdtemp(directory_template);
    assert(state_directory != nullptr);

    os::keys::KeyId key_id{};
    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope_v1{};
    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope_v2{};
    std::size_t envelope_v1_size = 0U;
    std::size_t envelope_v2_size = 0U;

    {
        auto server = spawn_server(state_directory);
        os::ipc::ClientConnection connection{server.channel};
        os::keys::KeyClient keys{connection};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

        {
            auto created = keys.create_application_data_key(scratch);
            assert(created);
            auto key = std::move(created).value();
            key_id = key.descriptor().id;
            assert(key.descriptor().version == 1U);

            auto encrypted_v1 = key.encrypt(
                as_bytes("before-service-restart-v1"),
                as_bytes("restart-bound-aad"),
                envelope_v1,
                scratch);
            assert(encrypted_v1);
            envelope_v1_size = encrypted_v1.value();

            auto rotated = key.rotate(scratch);
            assert(rotated);
            assert(rotated.value().version == 2U);

            auto encrypted_v2 = key.encrypt(
                as_bytes("before-service-restart-v2"),
                as_bytes("restart-bound-aad"),
                envelope_v2,
                scratch);
            assert(encrypted_v2);
            envelope_v2_size = encrypted_v2.value();
        }

        stop_server(server);
    }

    {
        auto server = spawn_server(state_directory);
        os::ipc::ClientConnection connection{server.channel};
        os::keys::KeyClient keys{connection};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

        {
            auto reopened = keys.open(key_id, scratch);
            assert(reopened);
            auto key = std::move(reopened).value();
            assert(key.descriptor().version == 2U);

            assert_plaintext(
                key,
                {envelope_v1.data(), envelope_v1_size},
                "before-service-restart-v1",
                scratch);
            assert_plaintext(
                key,
                {envelope_v2.data(), envelope_v2_size},
                "before-service-restart-v2",
                scratch);

            auto rotated = key.rotate(scratch);
            assert(rotated);
            assert(rotated.value().version == 3U);
        }

        stop_server(server);
    }

    {
        auto server = spawn_server(state_directory);
        os::ipc::ClientConnection connection{server.channel};
        os::keys::KeyClient keys{connection};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        {
            auto reopened = keys.open(key_id, scratch);
            assert(reopened);
            auto key = std::move(reopened).value();
            assert(key.descriptor().version == 3U);
            assert_plaintext(
                key,
                {envelope_v1.data(), envelope_v1_size},
                "before-service-restart-v1",
                scratch);
            assert_plaintext(
                key,
                {envelope_v2.data(), envelope_v2_size},
                "before-service-restart-v2",
                scratch);
        }
        stop_server(server);
    }

    std::array<char, 512U> registry_path{};
    const int formatted = std::snprintf(
        registry_path.data(),
        registry_path.size(),
        "%s/key-registry-v1.bin",
        state_directory);
    assert(formatted > 0);
    assert(static_cast<std::size_t>(formatted) < registry_path.size());
    (void)::unlink(registry_path.data());
    assert(::rmdir(state_directory) == 0);
    return 0;
}
