#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/storage/error.hpp>
#include <os/storage/path.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/supervisor.hpp>

namespace {

os::core::NativeHandle open_directory(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    constexpr os::sandbox::SandboxPolicyV1 storage_sandbox{
        .enabled = true,
        .require_no_new_privs = true,
        .clear_capabilities = true,
        .require_seccomp = true,
        .require_landlock = false,
        .max_open_files = 256U,
        .max_processes = 8U,
        .max_file_size_bytes = 1024U * 1024U,
    };
    constexpr os::supervisor::ServiceDescriptorV1 storage_descriptor{
        .service_id = os::storage::storage_service_id,
        .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x000000000000F020ULL},
        .user_id = os::core::UserId{0U},
        .name = "system.storage",
        .restart_policy = os::supervisor::RestartPolicy::on_failure,
        .restart_delay_ms = 10U,
        .max_restarts_in_window = 4U,
        .restart_window_ms = 5000U,
        .readiness_timeout_ms = 1000U,
        .sandbox = storage_sandbox,
    };

    os::supervisor::Supervisor supervisor({storage_descriptor, argv[1]});
    assert(supervisor.start());

    char root_template[] = "/tmp/enml-storage-revoke-XXXXXX";
    char* root_path = ::mkdtemp(root_template);
    assert(root_path != nullptr);
    auto authorized_root = open_directory(root_path);

    constexpr os::core::PrincipalId app_principal{
        0x4150500000000000ULL,
        0x0000000000002301ULL,
    };
    constexpr os::core::UserId app_user{23U};

    auto control_result = supervisor.connect_private_control();
    assert(control_result);
    auto control = std::move(control_result).value();
    os::storage::StorageControlClient root_control{control};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto published = root_control.register_private_root(
        app_principal,
        app_user,
        authorized_root,
        scratch,
        1000U);
    assert(published);

    auto identity = supervisor.register_process(::getpid(), app_principal, app_user);
    assert(identity);

    auto service_channel_result = supervisor.connect();
    assert(service_channel_result);
    auto service_channel = std::move(service_channel_result).value();
    os::ipc::ClientConnection service_connection{service_channel};
    os::storage::StorageClient storage{service_connection};

    auto old_root_result = storage.open_private_root(scratch);
    assert(old_root_result);
    auto old_root = std::move(old_root_result).value();

    auto before = os::storage::RelativePath::parse("before-revocation");
    assert(before);
    assert(old_root.create_directory(before.value(), scratch));

    // Revoking the profile root is a policy change, not merely a lookup-table
    // update. Every live bearer capability minted for this profile must be
    // closed by the service so possession cannot outlive authorization.
    auto revoked = root_control.unregister_private_root(
        app_principal,
        app_user,
        scratch,
        1000U);
    assert(revoked);

    auto stale = os::storage::RelativePath::parse("stale-after-revocation");
    assert(stale);
    auto stale_result = old_root.create_directory(stale.value(), scratch);
    assert(!stale_result);
    assert(stale_result.error().domain == os::core::ErrorDomain::ipc);
    assert(stale_result.error().code == os::ipc::errors::peer_died);

    auto missing_root = storage.open_private_root(scratch);
    assert(!missing_root);
    assert(missing_root.error().domain == os::core::ErrorDomain::storage);
    assert(missing_root.error().code == os::storage::errors::root_not_registered);

    // Re-publishing policy does not resurrect the stale endpoint. A new root
    // capability must be explicitly reacquired.
    published = root_control.register_private_root(
        app_principal,
        app_user,
        authorized_root,
        scratch,
        1000U);
    assert(published);

    auto new_root_result = storage.open_private_root(scratch);
    assert(new_root_result);
    auto new_root = std::move(new_root_result).value();
    auto after = os::storage::RelativePath::parse("after-republish");
    assert(after);
    assert(new_root.create_directory(after.value(), scratch));

    auto stale_again = old_root.create_directory(stale.value(), scratch);
    assert(!stale_again);
    assert(stale_again.error().domain == os::core::ErrorDomain::ipc);
    assert(stale_again.error().code == os::ipc::errors::peer_died);

    assert(supervisor.unregister_process(identity.value().peer.process));
    assert(root_control.unregister_private_root(
        app_principal,
        app_user,
        scratch,
        1000U));

    new_root = os::storage::DirectoryObjectHandle{};
    old_root = os::storage::DirectoryObjectHandle{};
    assert(::rmdir((std::string(root_path) + "/before-revocation").c_str()) == 0);
    assert(::rmdir((std::string(root_path) + "/after-republish").c_str()) == 0);
    assert(::rmdir(root_path) == 0);
    return 0;
}
