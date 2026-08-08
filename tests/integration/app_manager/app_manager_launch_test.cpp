#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/persistence.hpp>
#include <os/supervisor/supervisor.hpp>

namespace pkg = os::package;

namespace {

pkg::ApplicationIdentity make_application() {
    auto package_id = pkg::PackageId::parse("com.emnl.launchfixture");
    assert(package_id);
    pkg::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x51};
    signer.bytes[31] = std::byte{0xA7};
    return pkg::ApplicationIdentity{package_id.value(), signer};
}

pkg::ContentDigest make_digest(std::uint8_t seed) {
    pkg::ContentDigest digest{};
    digest.bytes[0] = static_cast<std::byte>(seed);
    digest.bytes[31] = static_cast<std::byte>(static_cast<std::uint8_t>(seed ^ 0xA5U));
    return digest;
}

pkg::PackageGenerationRecord make_generation(
    const pkg::ApplicationIdentity& application,
    std::uint64_t generation,
    std::uint8_t seed) {
    return pkg::PackageGenerationRecord{
        application,
        pkg::PackageGenerationId{generation},
        make_digest(seed),
    };
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

os::core::NativeHandle open_directory(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
}

os::core::NativeHandle open_directory(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
}

pkg::ManifestPath parse_manifest_path(std::string_view text) {
    auto path = pkg::ManifestPath::parse(text);
    assert(path);
    return path.value();
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
    assert(argc == 4);
    const char* echo_path = argv[1];
    const auto executable_v1 = split_executable(argv[2]);
    const auto executable_v2 = split_executable(argv[3]);

    constexpr os::supervisor::ServiceDescriptorV1 echo_descriptor{
        .service_id = os::core::ServiceId{0x0000F001U},
        .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x000000000000F001ULL},
        .user_id = os::core::UserId{0U},
        .name = "system.echo",
        .restart_policy = os::supervisor::RestartPolicy::on_failure,
        .restart_delay_ms = 25U,
        .max_restarts_in_window = 3U,
        .restart_window_ms = 2000U,
        .readiness_timeout_ms = 1000U,
        .sandbox = {},
    };
    os::supervisor::Supervisor supervisor({echo_descriptor, echo_path});
    assert(supervisor.start());

    char package_template[] = "/tmp/emnl-app-packages-XXXXXX";
    char principal_template[] = "/tmp/emnl-app-principals-XXXXXX";
    char data42_template[] = "/tmp/emnl-app-data42-XXXXXX";
    char data43_template[] = "/tmp/emnl-app-data43-XXXXXX";
    char* package_path = ::mkdtemp(package_template);
    char* principal_path = ::mkdtemp(principal_template);
    char* data42_path = ::mkdtemp(data42_template);
    char* data43_path = ::mkdtemp(data43_template);
    assert(package_path != nullptr && principal_path != nullptr);
    assert(data42_path != nullptr && data43_path != nullptr);

    auto package_opened = pkg::PersistentPackageRegistry::open(open_directory(package_path));
    auto principal_opened = os::app::ApplicationPrincipalStore::open(open_directory(principal_path));
    assert(package_opened && principal_opened);
    auto packages = std::move(package_opened).value();
    auto principals = std::move(principal_opened).value();

    const auto application = make_application();
    const auto generation1 = make_generation(application, 1U, 0x21U);
    const auto generation2 = make_generation(application, 2U, 0x22U);
    assert(packages.stage_generation(generation1));
    assert(packages.activate(application, generation1.generation));
    assert(packages.stage_generation(generation2));

    os::app::ApplicationManager manager(packages, principals, supervisor);

    // Final-component symlinks are never accepted as package executables.
    assert(::symlink(argv[2], (std::string(package_path) + "/bad-app").c_str()) == 0);
    auto symlink_target = manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation1,
        .generation_directory = open_directory(package_path),
        .entry_point = parse_manifest_path("bad-app"),
        .readiness_timeout_ms = 1000U,
    });
    assert(!symlink_target);
    assert(symlink_target.error().code == os::app::manager_errors::executable_rejected);

    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation1,
        .generation_directory = open_directory(executable_v1.directory),
        .entry_point = parse_manifest_path(executable_v1.name),
        .readiness_timeout_ms = 1000U,
    }));
    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation2,
        .generation_directory = open_directory(executable_v2.directory),
        .entry_point = parse_manifest_path(executable_v2.name),
        .readiness_timeout_ms = 1000U,
    }));

    const os::core::UserId user42{42U};
    const os::core::UserId user43{43U};
    const os::core::UserId missing_user{44U};
    assert(manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user42,
        .private_data_directory = open_directory(data42_path),
        .sandbox = {},
    }));
    assert(manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user43,
        .private_data_directory = open_directory(data43_path),
        .sandbox = {},
    }));

    // No profile means no launch and no accidental principal allocation.
    const auto before_missing = principals.record_count();
    auto missing_profile = manager.launch(application.package_id, missing_user);
    assert(!missing_profile);
    assert(missing_profile.error().code == os::app::manager_errors::profile_not_found);
    assert(principals.record_count() == before_missing);

    // Generation 2 is staged but not active. Same application+user receives a
    // stable principal while each process and application instance is fresh.
    auto first = manager.launch(application.package_id, user42);
    auto second = manager.launch(application.package_id, user42);
    assert(first && second);
    assert(first.value().generation == generation1.generation);
    assert(second.value().generation == generation1.generation);
    assert(first.value().identity.principal == second.value().identity.principal);
    assert(first.value().identity.principal.high == os::app::application_principal_high_v1);
    assert(first.value().identity.user == user42);
    assert(first.value().identity.process != second.value().identity.process);
    assert(first.value().instance != second.value().instance);

    auto other_user = manager.launch(application.package_id, user43);
    assert(other_user);
    assert(other_user.value().generation == generation1.generation);
    assert(other_user.value().identity.user == user43);
    assert(other_user.value().identity.principal != first.value().identity.principal);

    assert(packages.activate(application, generation2.generation));

    // Activation is prospective. Existing generation-1 processes stay bound
    // while future launches resolve generation 2 and retain the user's stable
    // application principal.
    auto still_first = manager.instance(first.value().instance);
    assert(still_first);
    assert(still_first.value().generation == generation1.generation);
    assert(::kill(still_first.value().native_pid, 0) == 0);

    auto third = manager.launch(application.package_id, user42);
    assert(third);
    assert(third.value().generation == generation2.generation);
    assert(third.value().content == generation2.content);
    assert(third.value().identity.principal == first.value().identity.principal);
    assert(third.value().identity.process != first.value().identity.process);

    auto persisted_principal = principals.lookup(application, user42);
    assert(persisted_principal);
    assert(persisted_principal.value().principal == first.value().identity.principal);

    auto unknown_id = pkg::PackageId::parse("com.emnl.notinstalled");
    assert(unknown_id);
    auto unknown_launch = manager.launch(unknown_id.value(), user42);
    assert(!unknown_launch);
    assert(unknown_launch.error().domain == os::core::ErrorDomain::package);
    assert(unknown_launch.error().code == pkg::errors::unknown_package);

    assert(manager.terminate(first.value().instance, SIGTERM));
    assert(wait_until_gone(manager, first.value().instance));
    auto revoked = supervisor.lookup_process(first.value().native_pid);
    assert(!revoked);
    assert(revoked.error().domain == os::core::ErrorDomain::security);

    assert(manager.terminate(second.value().instance, SIGTERM));
    assert(manager.terminate(other_user.value().instance, SIGTERM));
    assert(manager.terminate(third.value().instance, SIGTERM));
    assert(wait_until_gone(manager, second.value().instance));
    assert(wait_until_gone(manager, other_user.value().instance));
    assert(wait_until_gone(manager, third.value().instance));

    assert(::unlink((std::string(package_path) + "/bad-app").c_str()) == 0);
    remove_registry_files(package_path);
    remove_principal_files(principal_path);
    assert(::rmdir(package_path) == 0);
    assert(::rmdir(principal_path) == 0);
    assert(::rmdir(data42_path) == 0);
    assert(::rmdir(data43_path) == 0);
    return 0;
}
