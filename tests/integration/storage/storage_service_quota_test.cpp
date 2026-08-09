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

constexpr os::core::PeerIdentity test_identity{
    .principal = os::core::PrincipalId{0xA203040506070809ULL, 0xB203040506070809ULL},
    .user = os::core::UserId{19U},
    .process = os::core::ProcessId{81U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    explicit TestIdentityResolver(pid_t client_pid) noexcept
        : client_pid_(client_pid) {}

    os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id != static_cast<std::int64_t>(client_pid_) ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid())) {
            return os::core::make_error(os::core::ErrorDomain::security, 1U);
        }
        return test_identity;
    }

private:
    pid_t client_pid_ {-1};
};

[[noreturn]] void run_server(
    os::ipc::Channel channel,
    os::storage::PrivateRoot root,
    pid_t client_pid) {
    TestIdentityResolver resolver{client_pid};
    os::storage::PrivateRootRegistry roots;
    auto registered = roots.register_root(
        test_identity.principal,
        test_identity.user,
        std::move(root));
    if (!registered) std::_Exit(20);

    os::storage::StorageService service{channel, resolver, roots};
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    for (;;) {
        auto result = service.dispatch_once(receive_buffer, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                std::_Exit(0);
            }
            std::_Exit(21);
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

    const int root_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root_fd >= 0);
    auto root_result = os::storage::PrivateRoot::adopt_authorized_directory(
        os::core::NativeHandle{root_fd});
    assert(root_result);
    auto private_root = std::move(root_result).value();

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();

    const pid_t parent_pid = ::getpid();
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        channels[0].close();
        run_server(std::move(channels[1]), std::move(private_root), parent_pid);
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

    // Only one profile owns objects and the global table is still mostly free.
    // The next mint must therefore fail with the profile quota, not the global
    // object-table limit.
    auto denied = storage.open_private_root(scratch);
    assert(!denied);
    assert(denied.error().domain == os::core::ErrorDomain::storage);
    assert(denied.error().code == os::storage::errors::principal_object_limit);

    for (auto& root : roots) {
        root = os::storage::DirectoryObjectHandle{};
    }
    channels[0].close();

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    assert(::rmdir(root_path) == 0);
    return 0;
}
