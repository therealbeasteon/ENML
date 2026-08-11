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
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/core/native_handle.hpp>
#include <os/keys/service.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/persistence.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/process_authority.hpp>
#include <os/supervisor/service_broker.hpp>
#include <os/supervisor/supervisor.hpp>

namespace pkg = os::package;

namespace {

constexpr os::core::PrincipalId storage_service_principal{
    0x53595354454D0000ULL,
    0x000000000000F020ULL,
};
constexpr os::core::PrincipalId key_service_principal{
    0x4B45595345525631ULL,
    0x53595354454D3031ULL,
};

pkg::ApplicationIdentity make_application() {
    auto package_id = pkg::PackageId::parse("com.emnl.multiservicefixture");
    assert(package_id);
    pkg::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0xB2};
    signer.bytes[31] = std::byte{0x91};
    return pkg::ApplicationIdentity{package_id.value(), signer};
}

pkg::ContentDigest make_digest() {
    pkg::ContentDigest digest{};
    digest.bytes[0] = std::byte{0x29};
    digest.bytes[31] = std::byte{0xD4};
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

pkg::ManifestPath parse_manifest_path(std::string_view value) {
    auto parsed = pkg::ManifestPath::parse(value);
    assert(parsed);
    return parsed.value();
}

bool wait_until_gone(
    os::app::ApplicationManager& manager,
    os::core::ApplicationInstanceId instance) {
    // Bounded at 30 seconds rather than one, matching the M2.10 runtime
    // session fixture. Instance teardown waits on a child exiting and being
    // reaped, which is subject to scheduler variance, and this suite runs four
    // jobs concurrently on shared CI runners. No production timeout or
    // authorization rule is relaxed; this only widens how long the harness
    // waits for work the OS has already been asked to do.
    for (std::size_t attempt = 0U; attempt < 6000U; ++attempt) {
        timespec delay{.tv_sec = 0, .tv_nsec = 5'000'000L};
        (void)::nanosleep(&delay, nullptr);
        auto maintained = manager.maintain();
        assert(maintained);
        auto current = manager.instance(instance);
        if (!current) return current.error().code == os::app::manager_errors::unknown_instance;
    }
    return false;
}

void unlink_if_present(int directory_fd, const char* name) {
    if (::unlinkat(directory_fd, name, 0) != 0) assert(errno == ENOENT);
}

void assert_regular_nonempty(int directory_fd, const char* name) {
    struct stat metadata {};
    assert(::fstatat(directory_fd, name, &metadata, AT_SYMLINK_NOFOLLOW) == 0);
    assert(S_ISREG(metadata.st_mode));
    assert(metadata.st_size > 0);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 4);
    const char* storage_path = argv[1];
    const char* key_path = argv[2];
    const auto application_executable = split_executable(argv[3]);

    char key_state_template[] = "/tmp/emnl-m29-key-state-XXXXXX";
    char package_template[] = "/tmp/emnl-m29-packages-XXXXXX";
    char principal_template[] = "/tmp/emnl-m29-principals-XXXXXX";
    char data_template[] = "/tmp/emnl-m29-data-XXXXXX";
    char* key_state_path = ::mkdtemp(key_state_template);
    char* package_path = ::mkdtemp(package_template);
    char* principal_path = ::mkdtemp(principal_template);
    char* data_path = ::mkdtemp(data_template);
    assert(key_state_path != nullptr && package_path != nullptr);
    assert(principal_path != nullptr && data_path != nullptr);

    auto key_state_directory = open_directory(key_state_path);

    os::supervisor::ProcessAuthority process_authority;
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
    const os::supervisor::ServiceLaunchConfig storage_config{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = os::storage::storage_service_id,
            .principal_id = storage_service_principal,
            .user_id = os::core::UserId{0U},
            .name = "system.storage",
            .restart_policy = os::supervisor::RestartPolicy::on_failure,
            .restart_delay_ms = 20U,
            .max_restarts_in_window = 3U,
            .restart_window_ms = 2000U,
            .readiness_timeout_ms = 1000U,
            .sandbox = storage_sandbox,
        },
        .executable_path = storage_path,
    };
    const os::supervisor::ServiceLaunchConfig key_config{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = os::keys::key_service_id,
            .principal_id = key_service_principal,
            .user_id = os::core::UserId{0U},
            .name = "system.keys",
            .restart_policy = os::supervisor::RestartPolicy::on_failure,
            .restart_delay_ms = 20U,
            .max_restarts_in_window = 3U,
            .restart_window_ms = 2000U,
            .readiness_timeout_ms = 2000U,
            .sandbox = {},
        },
        .executable_path = key_path,
        .private_state_directory_fd = key_state_directory.native(),
    };

    os::supervisor::Supervisor storage_supervisor{storage_config, process_authority};
    os::supervisor::Supervisor key_supervisor{key_config, process_authority};
    assert(storage_supervisor.start());
    assert(key_supervisor.start());

    os::supervisor::ServiceBroker broker{process_authority};
    assert(broker.register_service(os::storage::storage_service_id, storage_supervisor));
    assert(broker.register_service(os::keys::key_service_id, key_supervisor));

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

    os::app::ApplicationManager manager(
        packages,
        principals,
        storage_supervisor,
        key_supervisor,
        broker);

    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation,
        .generation_directory = open_directory(application_executable.directory),
        .entry_point = parse_manifest_path(application_executable.name),
        .readiness_timeout_ms = 3000U,
    }));

    const os::core::UserId user{92U};
    assert(manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user,
        .private_data_directory = open_directory(data_path),
        .sandbox = {},
    }));

    auto launched = manager.launch(application.package_id, user);
    if (!launched) {
        std::fprintf(
            stderr,
            "m2.9 launch error domain=%u code=%u\n",
            static_cast<unsigned>(launched.error().domain),
            static_cast<unsigned>(launched.error().code));
    }
    assert(launched);
    const auto instance = launched.value();
    assert(instance.valid());

    // The same native child resolves to the same boot-scoped identity in both
    // services and in the broker authority. This is the M2.9 property that the
    // old independent single-service Supervisor model could not provide.
    auto storage_identity = storage_supervisor.lookup_process(instance.native_pid);
    auto key_identity = key_supervisor.lookup_process(instance.native_pid);
    auto broker_identity = broker.lookup(instance.identity.process);
    auto authority_identity = process_authority.lookup(instance.native_pid);
    assert(storage_identity && key_identity && broker_identity && authority_identity);
    assert(storage_identity.value().peer == instance.identity);
    assert(key_identity.value().peer == instance.identity);
    assert(broker_identity.value().peer == instance.identity);
    assert(authority_identity.value().peer == instance.identity);

    const int data_fd = ::open(data_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int key_state_fd = ::open(key_state_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(data_fd >= 0 && key_state_fd >= 0);
    assert_regular_nonempty(data_fd, "broker-v2-storage.bin");
    assert_regular_nonempty(key_state_fd, "key-registry-v1.bin");

    assert(manager.terminate(instance.instance, SIGTERM));
    assert(wait_until_gone(manager, instance.instance));
    assert(broker.process_count() == 0U);

    auto storage_gone = storage_supervisor.lookup_process(instance.native_pid);
    auto key_gone = key_supervisor.lookup_process(instance.native_pid);
    auto authority_gone = process_authority.lookup(instance.identity.process);
    assert(!storage_gone && !key_gone && !authority_gone);
    assert(storage_gone.error().domain == os::core::ErrorDomain::security);
    assert(key_gone.error().domain == os::core::ErrorDomain::security);
    assert(authority_gone.error().domain == os::core::ErrorDomain::security);

    assert(manager.uninstall_application(application));

    assert(::close(data_fd) == 0);
    assert(::close(key_state_fd) == 0);
    key_state_directory.reset();

    const int cleanup_data = ::open(data_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int cleanup_keys = ::open(key_state_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int cleanup_packages = ::open(package_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int cleanup_principals = ::open(principal_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(cleanup_data >= 0 && cleanup_keys >= 0 && cleanup_packages >= 0 && cleanup_principals >= 0);
    unlink_if_present(cleanup_data, "broker-v2-storage.bin");
    unlink_if_present(cleanup_keys, "key-registry-v1.bin");
    unlink_if_present(cleanup_keys, ".key-registry-v1.tmp");
    unlink_if_present(cleanup_packages, "registry-v1.bin");
    unlink_if_present(cleanup_packages, ".registry-v1.tmp");
    unlink_if_present(cleanup_principals, "principals-v1.bin");
    unlink_if_present(cleanup_principals, ".principals-v1.tmp");
    assert(::close(cleanup_data) == 0);
    assert(::close(cleanup_keys) == 0);
    assert(::close(cleanup_packages) == 0);
    assert(::close(cleanup_principals) == 0);

    assert(::rmdir(data_path) == 0);
    assert(::rmdir(key_state_path) == 0);
    assert(::rmdir(package_path) == 0);
    assert(::rmdir(principal_path) == 0);
    return 0;
}
