#include <algorithm>
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
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/error.hpp>
#include <os/keys/service.hpp>
#include <os/package/analyzer.hpp>
#include <os/package/persistence.hpp>
#include <os/storage/service.hpp>
#include <os/supervisor/supervisor.hpp>

namespace pkg = os::package;

namespace {

constexpr os::core::PrincipalId key_service_principal{
    0x4B45595345525631ULL,
    0x53595354454D3031ULL,
};

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

pkg::ApplicationIdentity make_application() {
    auto package_id = pkg::PackageId::parse("com.emnl.keypolicyfixture");
    assert(package_id);
    pkg::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0x6B};
    signer.bytes[31] = std::byte{0xD2};
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

void wait_for_key_generation(
    os::supervisor::Supervisor& supervisor,
    std::uint64_t previous_generation) {
    for (std::size_t attempt = 0U; attempt < 300U; ++attempt) {
        auto maintained = supervisor.maintain();
        if (!maintained && maintained.error().domain == os::core::ErrorDomain::service &&
            maintained.error().code == os::core::errors::service::crash_loop) {
            assert(false);
        }
        const auto status = supervisor.status();
        if (status.state == os::supervisor::ServiceState::running &&
            status.generation > previous_generation) {
            return;
        }
        ::usleep(10000U);
    }
    assert(false);
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

void assert_plaintext(
    os::keys::KeyObjectHandle& key,
    os::core::ByteSpan envelope,
    std::string_view expected,
    os::core::MutableByteSpan scratch) {
    std::array<std::byte, os::keys::max_key_plaintext_bytes> plaintext{};
    auto opened = key.decrypt(
        envelope,
        as_bytes("app-manager-key-policy-aad"),
        plaintext,
        scratch);
    assert(opened);
    assert(opened.value() == expected.size());
    assert(std::equal(
        plaintext.begin(),
        plaintext.begin() + static_cast<std::ptrdiff_t>(opened.value()),
        as_bytes(expected).begin()));
}

void unlink_if_present(int directory_fd, const char* name) {
    if (::unlinkat(directory_fd, name, 0) != 0) assert(errno == ENOENT);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 5);
    const char* storage_path = argv[1];
    const char* keys_path = argv[2];
    const auto executable_v1 = split_executable(argv[3]);
    const auto executable_v2 = split_executable(argv[4]);

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
        .restart_delay_ms = 25U,
        .max_restarts_in_window = 3U,
        .restart_window_ms = 2000U,
        .readiness_timeout_ms = 1000U,
        .sandbox = storage_sandbox,
    };
    os::supervisor::Supervisor storage_supervisor({storage_descriptor, storage_path});
    assert(storage_supervisor.start());

    char key_state_template[] = "/tmp/emnl-app-key-state-XXXXXX";
    char* key_state_path = ::mkdtemp(key_state_template);
    assert(key_state_path != nullptr);
    auto key_state_directory = open_directory(key_state_path);

    os::supervisor::ServiceLaunchConfig key_config{
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
        .executable_path = keys_path,
        .private_state_directory_fd = key_state_directory.native(),
    };
    os::supervisor::Supervisor key_supervisor{key_config};
    assert(key_supervisor.start());

    char package_template[] = "/tmp/emnl-key-policy-packages-XXXXXX";
    char principal_template[] = "/tmp/emnl-key-policy-principals-XXXXXX";
    char data_template[] = "/tmp/emnl-key-policy-data-XXXXXX";
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
    const auto generation1 = make_generation(application, 1U, 0x31U);
    const auto generation2 = make_generation(application, 2U, 0x32U);
    assert(packages.stage_generation(generation1));
    assert(packages.activate(application, generation1.generation));

    os::app::ApplicationManager manager(
        packages,
        principals,
        storage_supervisor,
        key_supervisor);

    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation1,
        .generation_directory = open_directory(executable_v1.directory),
        .entry_point = parse_manifest_path(executable_v1.name),
        .readiness_timeout_ms = 1000U,
    }));

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

    // The test process stands in for a future multi-service connection broker.
    // App Manager publishes only trusted lifecycle policy in M2.8; it does not
    // invent a second ProcessId for the launched application through the
    // single-service Key Supervisor prototype.
    auto key_identity = key_supervisor.register_process(::getpid(), principal, user);
    assert(key_identity);

    auto public_result = key_supervisor.connect();
    assert(public_result);
    auto public_channel = std::move(public_result).value();
    os::ipc::ClientConnection connection{public_channel};
    os::keys::KeyClient keys{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto created = keys.create_application_data_key(scratch);
    assert(created);
    auto key = std::move(created).value();
    const auto key_id = key.descriptor().id;

    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope{};
    auto encrypted = key.encrypt(
        as_bytes("retained-across-uninstall"),
        as_bytes("app-manager-key-policy-aad"),
        envelope,
        scratch);
    assert(encrypted);
    const std::size_t envelope_size = encrypted.value();

    auto first = manager.launch(application.package_id, user);
    assert(first);
    assert(first.value().generation == generation1.generation);

    // A Key Service crash loses generation-local hierarchy/policy state. Once
    // the system supervisor has restarted the service, App Manager detects the
    // new generation and republishes enabled profile/application policy without
    // caller-supplied owner or scope fields.
    const auto old_key_generation = key_supervisor.status().generation;
    assert(key_supervisor.terminate(SIGKILL));
    wait_for_key_generation(key_supervisor, old_key_generation);
    assert(manager.maintain());

    auto stale = key.decrypt(
        {envelope.data(), envelope_size},
        as_bytes("app-manager-key-policy-aad"),
        envelope,
        scratch);
    assert(!stale);
    assert(stale.error().domain == os::core::ErrorDomain::ipc);
    assert(stale.error().code == os::ipc::errors::peer_died);

    auto restarted_public_result = key_supervisor.connect();
    assert(restarted_public_result);
    auto restarted_public = std::move(restarted_public_result).value();
    os::ipc::ClientConnection restarted_connection{restarted_public};
    os::keys::KeyClient restarted_keys{restarted_connection};
    auto reopened = restarted_keys.open(key_id, scratch);
    assert(reopened);
    auto reopened_key = std::move(reopened).value();
    assert_plaintext(
        reopened_key,
        {envelope.data(), envelope_size},
        "retained-across-uninstall",
        scratch);

    // Uninstall revokes Key authority before process teardown. The logical key
    // and ciphertext remain durable, matching the separate lifecycle of
    // authorization versus destruction.
    assert(manager.uninstall_application(application));
    auto revoked = reopened_key.decrypt(
        {envelope.data(), envelope_size},
        as_bytes("app-manager-key-policy-aad"),
        envelope,
        scratch);
    assert(!revoked);
    assert(revoked.error().domain == os::core::ErrorDomain::ipc);
    assert(revoked.error().code == os::ipc::errors::peer_died);

    auto denied = restarted_keys.open(key_id, scratch);
    assert(!denied);
    assert(denied.error() == os::keys::key_error(os::keys::errors::policy_not_registered));
    assert(wait_until_gone(manager, first.value().instance));

    // Same-signer reinstall retains the durable principal and key state but
    // requires a fresh trusted publication. Launch performs that publication;
    // no stale KeyObject endpoint is resurrected.
    assert(packages.stage_generation(generation2));
    assert(manager.register_launch_target(os::app::LaunchTargetRegistration{
        .package = generation2,
        .generation_directory = open_directory(executable_v2.directory),
        .entry_point = parse_manifest_path(executable_v2.name),
        .readiness_timeout_ms = 1000U,
    }));
    assert(packages.activate(application, generation2.generation));

    auto second = manager.launch(application.package_id, user);
    assert(second);
    assert(second.value().generation == generation2.generation);
    assert(second.value().identity.principal == principal);

    auto retained_principal = principals.lookup(application, user);
    assert(retained_principal);
    assert(retained_principal.value().principal == principal);

    auto after_reinstall = restarted_keys.open(key_id, scratch);
    assert(after_reinstall);
    auto after_reinstall_key = std::move(after_reinstall).value();
    assert_plaintext(
        after_reinstall_key,
        {envelope.data(), envelope_size},
        "retained-across-uninstall",
        scratch);

    assert(manager.uninstall_application(application));
    assert(wait_until_gone(manager, second.value().instance));

    key_state_directory.reset();
    const int key_state_fd = ::open(key_state_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(key_state_fd >= 0);
    unlink_if_present(key_state_fd, "key-registry-v1.bin");
    unlink_if_present(key_state_fd, ".key-registry-v1.tmp");
    assert(::close(key_state_fd) == 0);

    const int package_fd = ::open(package_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int principal_fd = ::open(principal_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int data_fd = ::open(data_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(package_fd >= 0 && principal_fd >= 0 && data_fd >= 0);
    unlink_if_present(package_fd, "registry-v1.bin");
    unlink_if_present(package_fd, ".registry-v1.tmp");
    unlink_if_present(principal_fd, "principals-v1.bin");
    unlink_if_present(principal_fd, ".principals-v1.tmp");
    unlink_if_present(data_fd, "storage-probe.bin");
    assert(::close(package_fd) == 0);
    assert(::close(principal_fd) == 0);
    assert(::close(data_fd) == 0);

    assert(::rmdir(data_path) == 0);
    assert(::rmdir(principal_path) == 0);
    assert(::rmdir(package_path) == 0);
    assert(::rmdir(key_state_path) == 0);
    return 0;
}
