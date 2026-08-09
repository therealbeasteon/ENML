#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/persistence.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/supervisor.hpp>

namespace pkg = os::package;

namespace {

pkg::ApplicationIdentity make_application() {
    auto package_id = pkg::PackageId::parse("com.emnl.storagefixture");
    assert(package_id);
    pkg::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x72};
    signer.bytes[31] = std::byte{0x19};
    return pkg::ApplicationIdentity{package_id.value(), signer};
}

pkg::ContentDigest make_digest() {
    pkg::ContentDigest digest{};
    digest.bytes[0] = std::byte{0x34};
    digest.bytes[31] = std::byte{0xA1};
    return digest;
}

struct ExecutableLocation final {
    std::string directory;
    std::string name;
};

ExecutableLocation split_executable(const char* path) {
    assert(path != nullptr);
    std::string value(path);
    const auto separator = value.find_last_of('/');
    if (separator == std::string::npos) return ExecutableLocation{".", value};
    return ExecutableLocation{
        separator == 0U ? std::string{"/"} : value.substr(0U, separator),
        value.substr(separator + 1U),
    };
}

os::core::NativeHandle open_directory(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
}

os::core::NativeHandle open_directory(const std::string& path) {
    return open_directory(path.c_str());
}

pkg::ManifestPath parse_manifest_path(std::string_view text) {
    auto path = pkg::ManifestPath::parse(text);
    assert(path);
    return path.value();
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

bool wait_until_gone(
    os::app::ApplicationManager& manager,
    os::core::ApplicationInstanceId instance) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        timespec delay{.tv_sec = 0, .tv_nsec = 2'000'000L};
        (void)::nanosleep(&delay, nullptr);
        auto maintained = manager.maintain();
        assert(maintained);
        auto current = manager.instance(instance);
        if (!current) return current.error().code == os::app::manager_errors::unknown_instance;
    }
    return false;
}

class SupervisorStorageBridge final : public os::app::ApplicationStorageBridge {
public:
    explicit SupervisorStorageBridge(os::supervisor::Supervisor& supervisor) noexcept
        : supervisor_(supervisor) {}

    os::core::Result<void>
    provision_private_root(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const os::core::NativeHandle& private_data_directory) noexcept override {
        auto channel_result = supervisor_.connect();
        if (!channel_result) return channel_result.error();
        auto channel = std::move(channel_result).value();
        os::ipc::ClientConnection connection{channel};
        os::storage::StorageProvisioner provisioner{connection};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        return provisioner.provision_private_root(
            principal,
            user,
            private_data_directory,
            scratch);
    }

    os::core::Result<os::ipc::Channel>
    connect_application() noexcept override {
        return supervisor_.connect();
    }

private:
    os::supervisor::Supervisor& supervisor_;
};

} // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    const char* storage_service_path = argv[1];
    const auto app_executable = split_executable(argv[2]);

    constexpr os::supervisor::ServiceDescriptorV1 storage_descriptor{
        .service_id = os::storage::storage_service_id,
        .principal_id = os::core::PrincipalId{
            0x53595354454D0000ULL,
            0x000000000000F020ULL,
        },
        .user_id = os::core::UserId{0U},
        .name = "system.storage",
        .restart_policy = os::supervisor::RestartPolicy::on_failure,
        .restart_delay_ms = 25U,
        .max_restarts_in_window = 3U,
        .restart_window_ms = 2000U,
        .readiness_timeout_ms = 2000U,
        .sandbox = os::sandbox::SandboxPolicyV1{
            .enabled = true,
            .require_no_new_privs = true,
            .clear_capabilities = true,
            .require_seccomp = true,
            .require_landlock = false,
            .max_open_files = 256U,
            .max_processes = 8U,
            .max_file_size_bytes = 1024U * 1024U,
        },
    };

    os::supervisor::Supervisor supervisor({storage_descriptor, storage_service_path});
    assert(supervisor.start());

    auto admin_identity = supervisor.register_process(
        ::getpid(),
        os::storage::storage_profile_admin_principal,
        os::core::UserId{0U});
    assert(admin_identity);

    char package_template[] = "/tmp/emnl-storage-app-packages-XXXXXX";
    char principal_template[] = "/tmp/emnl-storage-app-principals-XXXXXX";
    char data_template[] = "/tmp/emnl-storage-app-data-XXXXXX";
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
        application,
        pkg::PackageGenerationId{1U},
        make_digest(),
    };
    assert(packages.stage_generation(generation));
    assert(packages.activate(application, generation.generation));

    os::app::ApplicationManager manager(packages, principals, supervisor);
    SupervisorStorageBridge bridge{supervisor};
    manager.attach_storage_bridge(bridge);

    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation,
        .generation_directory = open_directory(app_executable.directory),
        .entry_point = parse_manifest_path(app_executable.name),
        .readiness_timeout_ms = 2000U,
    }));

    const os::core::UserId user{42U};
    assert(manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user,
        .private_data_directory = open_directory(data_path),
        .sandbox = os::sandbox::SandboxPolicyV1{
            .enabled = true,
            .require_no_new_privs = true,
            .clear_capabilities = true,
            .require_seccomp = true,
            .require_landlock = false,
            .max_open_files = 32U,
            .max_processes = 4U,
            .max_file_size_bytes = 1024U * 1024U,
        },
    }));

    auto launched = manager.launch(application.package_id, user);
    assert(launched);
    assert(launched.value().identity.user == user);
    assert(wait_until_gone(manager, launched.value().instance));

    char stored_path[512]{};
    const int stored_length = std::snprintf(
        stored_path, sizeof(stored_path), "%s/brokered.txt", data_path);
    assert(stored_length > 0 && static_cast<std::size_t>(stored_length) < sizeof(stored_path));

    const int stored_fd = ::open(stored_path, O_RDONLY | O_CLOEXEC);
    assert(stored_fd >= 0);
    std::array<char, 8U> bytes{};
    assert(::read(stored_fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
    assert(::close(stored_fd) == 0);
    assert(std::string_view(bytes.data(), bytes.size()) == "brokered");

    auto unregistered = supervisor.unregister_process(admin_identity.value().peer.process);
    assert(unregistered);

    assert(::unlink(stored_path) == 0);
    remove_registry_files(package_path);
    remove_principal_files(principal_path);
    assert(::rmdir(data_path) == 0);
    assert(::rmdir(principal_path) == 0);
    assert(::rmdir(package_path) == 0);
    return 0;
}
