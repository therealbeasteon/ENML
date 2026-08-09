#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/package/package.hpp>
#include <os/package/persistence.hpp>
#include <os/storage/path.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/supervisor.hpp>

namespace pkg = os::package;

namespace {

pkg::ApplicationIdentity make_application() {
    auto package_id = pkg::PackageId::parse("com.emnl.storage.restart");
    assert(package_id);
    pkg::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x61};
    signer.bytes[31] = std::byte{0xB4};
    return pkg::ApplicationIdentity{package_id.value(), signer};
}

pkg::ContentDigest make_digest() {
    pkg::ContentDigest digest{};
    digest.bytes[0] = std::byte{0x31};
    digest.bytes[31] = std::byte{0xC2};
    return digest;
}

os::core::NativeHandle open_directory(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
}

void sleep_ms(long milliseconds) {
    const timespec delay{
        .tv_sec = milliseconds / 1000L,
        .tv_nsec = (milliseconds % 1000L) * 1'000'000L,
    };
    (void)::nanosleep(&delay, nullptr);
}

void remove_registry_files(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    static_cast<void>(::unlinkat(fd, "registry-v1.bin", 0));
    static_cast<void>(::unlinkat(fd, ".registry-v1.tmp", 0));
    assert(::close(fd) == 0);
}

void remove_principal_files(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    static_cast<void>(::unlinkat(fd, "principals-v1.bin", 0));
    static_cast<void>(::unlinkat(fd, ".principals-v1.tmp", 0));
    assert(::close(fd) == 0);
}

os::storage::DirectoryObjectHandle open_private_root(
    os::supervisor::Supervisor& supervisor) {
    auto channel_result = supervisor.connect();
    assert(channel_result);
    auto channel = std::move(channel_result).value();
    os::ipc::ClientConnection connection{channel};
    os::storage::StorageClient storage{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto root = storage.open_private_root(scratch);
    assert(root);
    return std::move(root).value();
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
    const auto first_service = supervisor.status();
    assert(first_service.state == os::supervisor::ServiceState::running);

    char package_template[] = "/tmp/emnl-storage-restart-packages-XXXXXX";
    char principal_template[] = "/tmp/emnl-storage-restart-principals-XXXXXX";
    char data_template[] = "/tmp/emnl-storage-restart-data-XXXXXX";
    char* package_path = ::mkdtemp(package_template);
    char* principal_path = ::mkdtemp(principal_template);
    char* data_path = ::mkdtemp(data_template);
    assert(package_path != nullptr && principal_path != nullptr && data_path != nullptr);

    auto package_opened = pkg::PersistentPackageRegistry::open(open_directory(package_path));
    auto principal_opened = os::app::ApplicationPrincipalStore::open(open_directory(principal_path));
    assert(package_opened && principal_opened);
    auto packages = std::move(package_opened).value();
    auto principals = std::move(principal_opened).value();

    const auto application = make_application();
    const pkg::PackageGenerationRecord generation{
        .application = application,
        .generation = pkg::PackageGenerationId{1U},
        .content = make_digest(),
    };
    assert(packages.stage_generation(generation));
    assert(packages.activate(application, generation.generation));

    os::app::ApplicationManager manager(packages, principals, supervisor);
    const os::core::UserId user{77U};
    assert(manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user,
        .private_data_directory = open_directory(data_path),
        .sandbox = {},
    }));

    auto principal_record = principals.lookup(application, user);
    assert(principal_record);
    const auto principal = principal_record.value().principal;

    auto first_identity = supervisor.register_process(::getpid(), principal, user);
    assert(first_identity);
    auto old_root = open_private_root(supervisor);

    auto before_path = os::storage::RelativePath::parse("before-restart");
    assert(before_path);
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    assert(old_root.create_directory(before_path.value(), scratch));
    assert(supervisor.unregister_process(first_identity.value().peer.process));

    assert(supervisor.terminate(SIGKILL));

    bool restarted = false;
    for (int attempt = 0; attempt < 300; ++attempt) {
        auto maintained = manager.maintain();
        if (!maintained && supervisor.status().state == os::supervisor::ServiceState::crash_loop) {
            break;
        }
        const auto status = supervisor.status();
        if (status.state == os::supervisor::ServiceState::running &&
            status.generation > first_service.generation &&
            status.identity.process != first_service.identity.process) {
            restarted = true;
            break;
        }
        sleep_ms(5L);
    }
    assert(restarted);

    // The App Manager is the trusted owner of profile publication. Once the
    // new Storage generation is running, maintain() republishes every retained
    // private root before clients are expected to reconnect.
    assert(manager.maintain());

    auto second_identity = supervisor.register_process(::getpid(), principal, user);
    assert(second_identity);
    assert(second_identity.value().peer.process != first_identity.value().peer.process);
    assert(second_identity.value().peer.principal == principal);

    auto new_root = open_private_root(supervisor);
    auto after_path = os::storage::RelativePath::parse("after-restart");
    assert(after_path);
    assert(new_root.create_directory(after_path.value(), scratch));

    // Object endpoints are generation-bound bearer capabilities. Restarting
    // Storage must not silently reconnect or resurrect a capability minted by
    // the dead service generation.
    auto stale_path = os::storage::RelativePath::parse("stale-endpoint");
    assert(stale_path);
    auto stale = old_root.create_directory(stale_path.value(), scratch);
    assert(!stale);
    assert(stale.error().domain == os::core::ErrorDomain::ipc);
    assert(stale.error().code == os::ipc::errors::peer_died);

    assert(supervisor.unregister_process(second_identity.value().peer.process));
    assert(::rmdir((std::string(data_path) + "/before-restart").c_str()) == 0);
    assert(::rmdir((std::string(data_path) + "/after-restart").c_str()) == 0);
    remove_registry_files(package_path);
    remove_principal_files(principal_path);
    assert(::rmdir(package_path) == 0);
    assert(::rmdir(principal_path) == 0);
    assert(::rmdir(data_path) == 0);
    return 0;
}
