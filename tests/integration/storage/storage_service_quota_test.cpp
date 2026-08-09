#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include <os/storage/error.hpp>
#include <os/storage/private_root.hpp>
#include <os/storage/service.hpp>

namespace {

constexpr os::core::PeerIdentity primary_identity{
    .principal = os::core::PrincipalId{0xA203040506070809ULL, 0xB203040506070809ULL},
    .user = os::core::UserId{19U},
    .process = os::core::ProcessId{81U},
};

constexpr os::core::PeerIdentity secondary_identity{
    .principal = os::core::PrincipalId{0xA303040506070809ULL, 0xB303040506070809ULL},
    .user = os::core::UserId{20U},
    .process = os::core::ProcessId{82U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    explicit TestIdentityResolver(pid_t primary_pid) noexcept
        : primary_pid_(primary_pid) {}

    os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id <= 0 ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid())) {
            return os::core::make_error(os::core::ErrorDomain::security, 1U);
        }
        if (credentials.process_id == static_cast<std::int64_t>(primary_pid_)) {
            return primary_identity;
        }
        // The only other sender in this integration test is the deliberately
        // forked secondary client. Per-message SCM_CREDENTIALS therefore makes
        // it a distinct principal even though both processes share the same
        // inherited SOCK_SEQPACKET endpoint.
        return secondary_identity;
    }

private:
    pid_t primary_pid_ {-1};
};

[[noreturn]] void run_server(
    os::ipc::Channel channel,
    os::storage::PrivateRoot primary_root,
    os::storage::PrivateRoot secondary_root,
    pid_t primary_pid) {
    TestIdentityResolver resolver{primary_pid};
    os::storage::PrivateRootRegistry roots;
    auto registered = roots.register_root(
        primary_identity.principal,
        primary_identity.user,
        std::move(primary_root));
    if (!registered) std::_Exit(20);
    registered = roots.register_root(
        secondary_identity.principal,
        secondary_identity.user,
        std::move(secondary_root));
    if (!registered) std::_Exit(21);

    os::storage::StorageService service{channel, resolver, roots};
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    for (;;) {
        auto result = service.dispatch_once(receive_buffer, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                std::_Exit(0);
            }
            std::_Exit(22);
        }
    }
}

} // namespace

int main() {
    static_assert(
        os::storage::max_storage_objects_per_principal < os::storage::max_storage_objects,
        "per-principal quota must remain below the global Storage object table");

    char root_template[] = "/tmp/enml-storage-quota-XXXXXX";
    char* root_path = ::mkdtemp(root_template);
    assert(root_path != nullptr);

    const int primary_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int secondary_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(primary_fd >= 0 && secondary_fd >= 0);
    auto primary_root_result = os::storage::PrivateRoot::adopt_authorized_directory(
        os::core::NativeHandle{primary_fd});
    auto secondary_root_result = os::storage::PrivateRoot::adopt_authorized_directory(
        os::core::NativeHandle{secondary_fd});
    assert(primary_root_result && secondary_root_result);
    auto primary_root = std::move(primary_root_result).value();
    auto secondary_root = std::move(secondary_root_result).value();

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();

    const pid_t parent_pid = ::getpid();
    const pid_t server = ::fork();
    assert(server >= 0);
    if (server == 0) {
        channels[0].close();
        run_server(
            std::move(channels[1]),
            std::move(primary_root),
            std::move(secondary_root),
            parent_pid);
    }

    channels[1].close();
    os::ipc::ClientConnection connection{channels[0]};
    os::storage::StorageClient storage{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    std::array<os::storage::DirectoryObjectHandle,
               os::storage::max_storage_objects_per_principal> roots{};

    for (auto& root : roots) {
        auto opened = storage.open_private_root(scratch);
        assert(opened);
        root = std::move(opened).value();
    }

    // The primary profile owns its entire budget while the global table still
    // has ample room. Its next mint must therefore report the profile quota.
    auto denied = storage.open_private_root(scratch);
    assert(!denied);
    assert(denied.error().domain == os::core::ErrorDomain::storage);
    assert(denied.error().code == os::storage::errors::principal_object_limit);

    // A second process using the inherited transport is authenticated from its
    // own packet credentials and maps to another PrincipalId/UserId. Its root
    // mint must still succeed, proving one profile cannot consume another
    // profile's object budget.
    const pid_t secondary_client = ::fork();
    assert(secondary_client >= 0);
    if (secondary_client == 0) {
        auto opened = storage.open_private_root(scratch);
        if (!opened) std::_Exit(30);
        std::_Exit(0);
    }
    int secondary_status = 0;
    assert(::waitpid(secondary_client, &secondary_status, 0) == secondary_client);
    assert(WIFEXITED(secondary_status));
    assert(WEXITSTATUS(secondary_status) == 0);

    for (auto& root : roots) {
        root = os::storage::DirectoryObjectHandle{};
    }
    channels[0].close();

    int server_status = 0;
    assert(::waitpid(server, &server_status, 0) == server);
    assert(WIFEXITED(server_status));
    assert(WEXITSTATUS(server_status) == 0);
    assert(::rmdir(root_path) == 0);
    return 0;
}
