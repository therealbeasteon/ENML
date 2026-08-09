#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/package/package.hpp>
#include <os/package/persistence.hpp>
#include <os/storage/error.hpp>
#include <os/storage/path.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/supervisor.hpp>

namespace pkg = os::package;

namespace {

pkg::ApplicationIdentity make_application() {
    auto package_id = pkg::PackageId::parse("com.enml.storage.uninstall");
    assert(package_id);
    pkg::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x71};
    signer.bytes[31] = std::byte{0xD4};
    return pkg::ApplicationIdentity{package_id.value(), signer};
}

pkg::ContentDigest make_digest() {
    pkg::ContentDigest digest{};
    digest.bytes[0] = std::byte{0x41};
    digest.bytes[31] = std::byte{0xE3};
    return digest;
}

os::core::NativeHandle open_directory(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
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

    char package_template[] = "/tmp/enml-uninstall-packages-XXXXXX";
    char principal_template[] = "/tmp/enml-uninstall-principals-XXXXXX";
    char data_template[] = "/tmp/enml-uninstall-data-XXXXXX";
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
    const os::core::UserId user{91U};
    assert(manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user,
        .private_data_directory = open_directory(data_path),
        .sandbox = {},
    }));

    auto principal_record = principals.lookup(application, user);
    assert(principal_record);
    const auto principal = principal_record.value().principal;

    // The test process temporarily acts as the application principal so it can
    // hold a real bearer capability across the uninstall boundary.
    auto identity = supervisor.register_process(::getpid(), principal, user);
    assert(identity);

    auto channel_result = supervisor.connect();
    assert(channel_result);
    auto channel = std::move(channel_result).value();
    os::ipc::ClientConnection connection{channel};
    os::storage::StorageClient storage{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto root_result = storage.open_private_root(scratch);
    assert(root_result);
    auto root = std::move(root_result).value();
    auto before = os::storage::RelativePath::parse("before-uninstall");
    assert(before);
    assert(root.create_directory(before.value(), scratch));

    // Uninstall removes launch eligibility and live Storage policy, but does
    // not delete the retained data directory or durable application principal.
    assert(manager.uninstall_application(application));

    auto no_active = packages.active(application);
    assert(!no_active);
    assert(no_active.error().domain == os::core::ErrorDomain::package);
    assert(no_active.error().code == pkg::errors::no_active_generation);

    auto stale = os::storage::RelativePath::parse("after-uninstall-stale");
    assert(stale);
    auto stale_result = root.create_directory(stale.value(), scratch);
    assert(!stale_result);
    assert(stale_result.error().domain == os::core::ErrorDomain::ipc);
    assert(stale_result.error().code == os::ipc::errors::peer_died);

    auto missing_root = storage.open_private_root(scratch);
    assert(!missing_root);
    assert(missing_root.error().domain == os::core::ErrorDomain::storage);
    assert(missing_root.error().code == os::storage::errors::root_not_registered);

    // The data itself survives uninstall. Only authority was revoked.
    struct stat data_info {};
    assert(::fstatat(
        open_directory(data_path).native(),
        "before-uninstall",
        &data_info,
        AT_SYMLINK_NOFOLLOW) == 0);
    assert(S_ISDIR(data_info.st_mode));

    auto retained_principal = principals.lookup(application, user);
    assert(retained_principal);
    assert(retained_principal.value().principal == principal);

    assert(supervisor.unregister_process(identity.value().peer.process));
    root = os::storage::DirectoryObjectHandle{};
    assert(::rmdir((std::string(data_path) + "/before-uninstall").c_str()) == 0);
    remove_registry_files(package_path);
    remove_principal_files(principal_path);
    assert(::rmdir(package_path) == 0);
    assert(::rmdir(principal_path) == 0);
    assert(::rmdir(data_path) == 0);
    return 0;
}
