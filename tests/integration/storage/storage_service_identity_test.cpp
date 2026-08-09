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
#include <os/storage/private_root.hpp>
#include <os/storage/service.hpp>

namespace {

constexpr os::core::PeerIdentity parent_identity{
    .principal = os::core::PrincipalId{0x1301020304050607ULL, 0x2401020304050607ULL},
    .user = os::core::UserId{11U},
    .process = os::core::ProcessId{81U},
};

class ParentOnlyResolver final : public os::ipc::PeerIdentityResolver {
public:
    explicit ParentOnlyResolver(pid_t expected_pid) noexcept
        : expected_pid_(expected_pid) {}

    os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id != static_cast<std::int64_t>(expected_pid_) ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid())) {
            return os::core::make_error(
                os::core::ErrorDomain::security,
                os::core::errors::security::unknown_process);
        }
        return parent_identity;
    }

private:
    pid_t expected_pid_ {-1};
};

[[noreturn]] void run_server(
    os::ipc::Channel channel,
    os::storage::PrivateRoot root,
    pid_t expected_client_pid) {
    ParentOnlyResolver resolver{expected_client_pid};
    os::storage::PrivateRootRegistry roots;
    auto registered = roots.register_root(
        parent_identity.principal,
        parent_identity.user,
        std::move(root));
    if (!registered) std::_Exit(30);

    os::storage::StorageService service{channel, resolver, roots};
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    for (;;) {
        auto result = service.dispatch_once(receive_buffer, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                std::_Exit(0);
            }
            std::_Exit(31);
        }
    }
}

} // namespace

int main() {
    char root_template[] = "/tmp/emnl-storage-identity-XXXXXX";
    char* root_path = ::mkdtemp(root_template);
    assert(root_path != nullptr);

    const int root_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root_fd >= 0);
    auto root_result = os::storage::PrivateRoot::adopt_authorized_directory(
        os::core::NativeHandle{root_fd});
    assert(root_result);
    auto root = std::move(root_result).value();

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();

    const pid_t trusted_parent_pid = ::getpid();
    const pid_t server = ::fork();
    assert(server >= 0);
    if (server == 0) {
        channels[0].close();
        run_server(std::move(channels[1]), std::move(root), trusted_parent_pid);
    }
    channels[1].close();

    // The attacker inherits the exact same connected endpoint. Per-message
    // SCM_CREDENTIALS must identify the child PID, so possession of the main
    // Storage Service connection does not let it borrow the parent's identity.
    const pid_t attacker = ::fork();
    assert(attacker >= 0);
    if (attacker == 0) {
        os::ipc::ClientConnection attacker_connection{channels[0]};
        os::storage::StorageClient attacker_client{attacker_connection};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto result = attacker_client.open_private_root(scratch);
        if (result) std::_Exit(40);
        if (result.error().domain != os::core::ErrorDomain::security ||
            result.error().code != os::core::errors::security::unknown_process) {
            std::_Exit(41);
        }
        std::_Exit(0);
    }

    int attacker_status = 0;
    assert(::waitpid(attacker, &attacker_status, 0) == attacker);
    assert(WIFEXITED(attacker_status));
    assert(WEXITSTATUS(attacker_status) == 0);

    // The original registered process still resolves and receives its own root
    // capability after the rejected inherited-channel attempt.
    os::ipc::ClientConnection connection{channels[0]};
    os::storage::StorageClient client{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto root_handle = client.open_private_root(scratch);
    assert(root_handle);
    assert(root_handle.value().rights() == os::storage::directory_rights::all);

    root_handle = os::core::Result<os::storage::DirectoryObjectHandle>{
        os::storage::DirectoryObjectHandle{}};
    channels[0].close();

    int server_status = 0;
    assert(::waitpid(server, &server_status, 0) == server);
    assert(WIFEXITED(server_status));
    assert(WEXITSTATUS(server_status) == 0);

    assert(::rmdir(root_path) == 0);
    return 0;
}
