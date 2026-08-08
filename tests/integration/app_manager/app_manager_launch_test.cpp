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
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
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

    char state_template[] = "/tmp/emnl-app-manager-XXXXXX";
    char* state_path = ::mkdtemp(state_template);
    assert(state_path != nullptr);

    auto opened = pkg::PersistentPackageRegistry::open(open_directory(state_path));
    assert(opened);
    auto packages = std::move(opened).value();

    const auto application = make_application();
    const auto generation1 = make_generation(application, 1U, 0x21U);
    const auto generation2 = make_generation(application, 2U, 0x22U);
    const auto generation3 = make_generation(application, 3U, 0x23U);
    assert(packages.stage_generation(generation1));
    assert(packages.activate(application, generation1.generation));
    assert(packages.stage_generation(generation2));
    assert(packages.stage_generation(generation3));

    constexpr os::core::PrincipalId app_principal{
        0x4150502E454D4E4CULL,
        0x0000000000000042ULL,
    };

    os::app::ApplicationManager manager(packages, supervisor);

    // Final-component symlinks are not accepted as package executables.
    assert(::symlink(argv[2], (std::string(state_path) + "/bad-app").c_str()) == 0);
    auto symlink_target = manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation1,
        .principal = app_principal,
        .generation_directory = open_directory(state_path),
        .entry_point = parse_manifest_path("bad-app"),
        .sandbox = {},
        .readiness_timeout_ms = 1000U,
    });
    assert(!symlink_target);
    assert(symlink_target.error().code == os::app::manager_errors::executable_rejected);

    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation1,
        .principal = app_principal,
        .generation_directory = open_directory(executable_v1.directory),
        .entry_point = parse_manifest_path(executable_v1.name),
        .sandbox = {},
        .readiness_timeout_ms = 1000U,
    }));
    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation2,
        .principal = app_principal,
        .generation_directory = open_directory(executable_v2.directory),
        .entry_point = parse_manifest_path(executable_v2.name),
        .sandbox = {},
        .readiness_timeout_ms = 1000U,
    }));

    // One signer-bound application keeps one principal across generations.
    auto mismatched_principal = manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation3,
        .principal = os::core::PrincipalId{0xBAD0ULL, 0xBEEF0ULL},
        .generation_directory = open_directory(executable_v2.directory),
        .entry_point = parse_manifest_path(executable_v2.name),
        .sandbox = {},
        .readiness_timeout_ms = 1000U,
    });
    assert(!mismatched_principal);
    assert(mismatched_principal.error().code == os::app::manager_errors::principal_mismatch);

    const os::core::UserId user{42U};

    // Generation 2 is staged but not active, so both launches must execute the
    // generation-1 fixture. That binary refuses READY for any other generation.
    auto first = manager.launch(application.package_id, user);
    assert(first);
    auto second = manager.launch(application.package_id, user);
    assert(second);
    assert(first.value().generation == generation1.generation);
    assert(second.value().generation == generation1.generation);
    assert(first.value().identity.principal == app_principal);
    assert(second.value().identity.principal == app_principal);
    assert(first.value().identity.user == user);
    assert(first.value().identity.process != second.value().identity.process);
    assert(first.value().instance != second.value().instance);

    assert(packages.activate(application, generation2.generation));

    // Activation is prospective. Existing processes remain permanently bound
    // to their launch generation while a new launch resolves generation 2.
    auto still_first = manager.instance(first.value().instance);
    assert(still_first);
    assert(still_first.value().generation == generation1.generation);
    assert(::kill(still_first.value().native_pid, 0) == 0);

    auto third = manager.launch(application.package_id, user);
    assert(third);
    assert(third.value().generation == generation2.generation);
    assert(third.value().content == generation2.content);
    assert(third.value().identity.principal == app_principal);
    assert(third.value().identity.process != first.value().identity.process);

    auto unknown_id = pkg::PackageId::parse("com.emnl.notinstalled");
    assert(unknown_id);
    auto unknown_launch = manager.launch(unknown_id.value(), user);
    assert(!unknown_launch);
    assert(unknown_launch.error().domain == os::core::ErrorDomain::package);
    assert(unknown_launch.error().code == pkg::errors::unknown_package);

    assert(manager.terminate(first.value().instance, SIGTERM));
    assert(wait_until_gone(manager, first.value().instance));
    auto revoked = supervisor.lookup_process(first.value().native_pid);
    assert(!revoked);
    assert(revoked.error().domain == os::core::ErrorDomain::security);

    assert(::unlink((std::string(state_path) + "/bad-app").c_str()) == 0);
    assert(manager.terminate(second.value().instance, SIGTERM));
    assert(manager.terminate(third.value().instance, SIGTERM));
    assert(wait_until_gone(manager, second.value().instance));
    assert(wait_until_gone(manager, third.value().instance));

    const int state_fd = ::open(state_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(state_fd >= 0);
    static_cast<void>(::unlinkat(state_fd, "registry-v1.bin", 0));
    static_cast<void>(::unlinkat(state_fd, ".registry-v1.tmp", 0));
    assert(::close(state_fd) == 0);
    assert(::rmdir(state_path) == 0);
    return 0;
}
