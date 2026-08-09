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
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/persistence.hpp>
#include <os/storage/service.hpp>
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
    assert(argc == 5);
    const char* storage_path = argv[1];
    const auto executable_v1 = split_executable(argv[2]);
    const auto executable_v2 = split_executable(argv[3]);
    const auto executable_v3 = split_executable(argv[4]);

    constexpr os::supervisor::ServiceDescriptorV1 storage_descriptor{
        .service_id = os::storage::storage_service_id,
        .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x000000000000F020ULL},
        .user_id = os::core::UserId{0U},
        .name = "system.storage",
        .restart_policy = os::supervisor::RestartPolicy::on_failure,
        .restart_delay_ms = 25U,
        .max_restarts_in_window = 3U,
        .restart_window_ms = 2000U,
        .readiness_timeout_ms = 1000U,
        .sandbox = {},
    };
    os::supervisor::Supervisor supervisor({storage_descriptor, storage_path});
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
    const auto generation3 = make_generation(application, 3U, 0x23U);
    assert(packages.stage_generation(generation1));
    assert(packages.activate(application, generation1.generation));
    assert(packages.stage_generation(generation2));

    os::app::ApplicationManager manager(packages, principals, supervisor);

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
    auto profile42 = manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user42,
        .private_data_directory = open_directory(data42_path),
        .sandbox = {},
    });
    if (!profile42) {
        std::fprintf(stderr, "profile42 error domain=%u code=%u\n",
            static_cast<unsigned>(profile42.error().domain),
            static_cast<unsigned>(profile42.error().code));
    }
    assert(profile42);
    auto profile43 = manager.register_application_profile(os::app::ApplicationProfileRegistration{
        .application = application,
        .user = user43,
        .private_data_directory = open_directory(data43_path),
        .sandbox = {},
    });
    if (!profile43) {
        std::fprintf(stderr, "profile43 error domain=%u code=%u\n",
            static_cast<unsigned>(profile43.error().domain),
            static_cast<unsigned>(profile43.error().code));
    }
    assert(profile43);

    const auto before_missing = principals.record_count();
    auto missing_profile = manager.launch(application.package_id, missing_user);
    assert(!missing_profile);
    assert(missing_profile.error().code == os::app::manager_errors::profile_not_found);
    assert(principals.record_count() == before_missing);

    auto first = manager.launch(application.package_id, user42);
    auto second = manager.launch(application.package_id, user42);
    auto other_user = manager.launch(application.package_id, user43);
    assert(first && second && other_user);
    assert(first.value().generation == generation1.generation);
    assert(second.value().generation == generation1.generation);
    assert(other_user.value().generation == generation1.generation);
    assert(first.value().identity.principal == second.value().identity.principal);
    assert(other_user.value().identity.principal != first.value().identity.principal);
    assert(first.value().identity.principal.high == os::app::application_principal_high_v1);
    assert(first.value().identity.user == user42);
    assert(other_user.value().identity.user == user43);
    assert(first.value().identity.process != second.value().identity.process);
    assert(first.value().instance != second.value().instance);

    auto generation1_pins = manager.generation_pin_count(application, generation1.generation);
    assert(generation1_pins && generation1_pins.value() == 3U);

    assert(packages.activate(application, generation2.generation));

    auto retire_active = manager.retire_launch_target(application, generation2.generation);
    assert(!retire_active);
    assert(retire_active.error().code == os::app::manager_errors::generation_active);

    auto retire_pinned = manager.retire_launch_target(application, generation1.generation);
    assert(!retire_pinned);
    assert(retire_pinned.error().code == os::app::manager_errors::generation_in_use);

    auto third = manager.launch(application.package_id, user42);
    assert(third);
    assert(third.value().generation == generation2.generation);
    assert(third.value().content == generation2.content);
    assert(third.value().identity.principal == first.value().identity.principal);
    assert(third.value().identity.process != first.value().identity.process);

    auto generation2_pins = manager.generation_pin_count(application, generation2.generation);
    assert(generation2_pins && generation2_pins.value() == 1U);

    assert(manager.terminate(first.value().instance, SIGTERM));
    assert(manager.terminate(second.value().instance, SIGTERM));
    assert(wait_until_gone(manager, first.value().instance));
    assert(wait_until_gone(manager, second.value().instance));
    generation1_pins = manager.generation_pin_count(application, generation1.generation);
    assert(generation1_pins && generation1_pins.value() == 1U);
    retire_pinned = manager.retire_launch_target(application, generation1.generation);
    assert(!retire_pinned);
    assert(retire_pinned.error().code == os::app::manager_errors::generation_in_use);

    auto persisted_principal = principals.lookup(application, user42);
    assert(persisted_principal);
    const auto user42_principal = persisted_principal.value().principal;
    assert(user42_principal == first.value().identity.principal);

    auto unknown_id = pkg::PackageId::parse("com.emnl.notinstalled");
    assert(unknown_id);
    auto unknown_launch = manager.launch(unknown_id.value(), user42);
    assert(!unknown_launch);
    assert(unknown_launch.error().domain == os::core::ErrorDomain::package);
    assert(unknown_launch.error().code == pkg::errors::unknown_package);

    assert(manager.uninstall_application(application));
    auto no_active = packages.active(application);
    assert(!no_active);
    assert(no_active.error().code == pkg::errors::no_active_generation);

    auto revoked_old = supervisor.lookup_process(other_user.value().native_pid);
    auto revoked_new = supervisor.lookup_process(third.value().native_pid);
    assert(!revoked_old && !revoked_new);
    assert(revoked_old.error().domain == os::core::ErrorDomain::security);
    assert(revoked_new.error().domain == os::core::ErrorDomain::security);

    auto launch_after_uninstall = manager.launch(application.package_id, user42);
    assert(!launch_after_uninstall);
    assert(launch_after_uninstall.error().domain == os::core::ErrorDomain::package);
    assert(launch_after_uninstall.error().code == pkg::errors::no_active_generation);

    assert(wait_until_gone(manager, other_user.value().instance));
    assert(wait_until_gone(manager, third.value().instance));
    generation1_pins = manager.generation_pin_count(application, generation1.generation);
    generation2_pins = manager.generation_pin_count(application, generation2.generation);
    assert(generation1_pins && generation1_pins.value() == 0U);
    assert(generation2_pins && generation2_pins.value() == 0U);

    assert(manager.retire_launch_target(application, generation1.generation));
    assert(manager.retire_launch_target(application, generation2.generation));

    auto retired_again = manager.retire_launch_target(application, generation1.generation);
    assert(!retired_again);
    assert(retired_again.error().code == os::app::manager_errors::target_not_found);

    persisted_principal = principals.lookup(application, user42);
    assert(persisted_principal);
    assert(persisted_principal.value().principal == user42_principal);

    assert(packages.stage_generation(generation3));
    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation3,
        .generation_directory = open_directory(executable_v3.directory),
        .entry_point = parse_manifest_path(executable_v3.name),
        .readiness_timeout_ms = 1000U,
    }));
    assert(packages.activate(application, generation3.generation));

    auto reinstalled = manager.launch(application.package_id, user42);
    assert(reinstalled);
    assert(reinstalled.value().generation == generation3.generation);
    assert(reinstalled.value().identity.principal == user42_principal);
    assert(reinstalled.value().identity.process != third.value().identity.process);
    assert(reinstalled.value().instance != third.value().instance);

    assert(manager.uninstall_application(application));
    assert(wait_until_gone(manager, reinstalled.value().instance));
    assert(manager.retire_launch_target(application, generation3.generation));
    persisted_principal = principals.lookup(application, user42);
    assert(persisted_principal);
    assert(persisted_principal.value().principal == user42_principal);

    assert(::unlink((std::string(package_path) + "/bad-app").c_str()) == 0);
    static_cast<void>(::unlink((std::string(data42_path) + "/storage-probe.bin").c_str()));
    static_cast<void>(::unlink((std::string(data43_path) + "/storage-probe.bin").c_str()));
    remove_registry_files(package_path);
    remove_principal_files(principal_path);
    assert(::rmdir(package_path) == 0);
    assert(::rmdir(principal_path) == 0);
    assert(::rmdir(data42_path) == 0);
    assert(::rmdir(data43_path) == 0);
    return 0;
}
