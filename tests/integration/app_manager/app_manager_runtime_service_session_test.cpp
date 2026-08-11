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

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <os/app/manager.hpp>
#include <os/app/principal_store.hpp>
#include <os/collection/session.hpp>
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
constexpr os::core::PeerIdentity collection_consumer_peer{
    .principal = os::collection::collection_consumer_principal,
    .user = os::core::UserId{0U},
    .process = os::core::ProcessId{0xC011EC7U},
};

pkg::ApplicationIdentity make_application() {
    auto package_id = pkg::PackageId::parse("com.emnl.runtimesessionfixture");
    assert(package_id);
    pkg::SignerLineageId signer{};
    signer.bytes[0] = std::byte{0xD2};
    signer.bytes[31] = std::byte{0x10};
    return pkg::ApplicationIdentity{package_id.value(), signer};
}

pkg::ContentDigest make_digest() {
    pkg::ContentDigest digest{};
    digest.bytes[0] = std::byte{0x4A};
    digest.bytes[31] = std::byte{0x10};
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

void short_delay() noexcept {
    timespec delay{.tv_sec = 0, .tv_nsec = 5'000'000L};
    while (::nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
}

bool wait_for_file(
    os::app::ApplicationManager& manager,
    int directory_fd,
    const char* name) {
    // This fixture deliberately stacks old-capability death observation,
    // supervisor restart/readiness, policy republish and explicit application
    // reacquisition. The child itself has two separately bounded retry phases,
    // so the outer lifecycle observer must cover their combined worst-case plus
    // scheduler variance. Keep the integration timeout bounded at 30 seconds;
    // no production service timeout or authorization rule is relaxed here.
    for (std::size_t attempt = 0U; attempt < 6000U; ++attempt) {
        auto maintained = manager.maintain();
        if (!maintained) {
            std::fprintf(
                stderr,
                "m2.10 maintain error domain=%u code=%u while waiting for %s\n",
                static_cast<unsigned>(maintained.error().domain),
                static_cast<unsigned>(maintained.error().code),
                name);
            return false;
        }

        struct stat metadata {};
        if (::fstatat(directory_fd, name, &metadata, AT_SYMLINK_NOFOLLOW) == 0) {
            return S_ISREG(metadata.st_mode) && metadata.st_size > 0;
        }
        if (errno != ENOENT) return false;
        short_delay();
    }
    return false;
}

bool wait_until_gone(
    os::app::ApplicationManager& manager,
    os::core::ApplicationInstanceId instance) {
    for (std::size_t attempt = 0U; attempt < 400U; ++attempt) {
        short_delay();
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

void cleanup_crash_left_atomic_temps(int directory_fd) {
    // This test deliberately SIGKILLs system.storage while an atomic replace
    // may be between O_EXCL temp creation and rename. The final target remains
    // atomic, but a process death can legitimately leave the private temporary
    // inode behind. Remove only the implementation-reserved test residue so the
    // mkdtemp fixture itself can be deleted deterministically.
    const int scan_fd = ::dup(directory_fd);
    assert(scan_fd >= 0);
    DIR* directory = ::fdopendir(scan_fd);
    assert(directory != nullptr);

    constexpr std::string_view prefix = ".emnl-atomic-";
    errno = 0;
    while (dirent* entry = ::readdir(directory)) {
        const std::string_view name{entry->d_name};
        if (name.starts_with(prefix)) {
            assert(::unlinkat(directory_fd, entry->d_name, 0) == 0 || errno == ENOENT);
        }
        errno = 0;
    }
    assert(errno == 0);
    assert(::closedir(directory) == 0);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 4);
    const char* storage_path = argv[1];
    const char* key_path = argv[2];
    const auto application_executable = split_executable(argv[3]);

    char key_state_template[] = "/tmp/emnl-m210-key-state-XXXXXX";
    char package_template[] = "/tmp/emnl-m210-packages-XXXXXX";
    char principal_template[] = "/tmp/emnl-m210-principals-XXXXXX";
    char data_template[] = "/tmp/emnl-m210-data-XXXXXX";
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
            .max_restarts_in_window = 4U,
            .restart_window_ms = 3000U,
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
            .max_restarts_in_window = 4U,
            .restart_window_ms = 3000U,
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

    const os::core::UserId user{210U};
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
            "m2.10 launch error domain=%u code=%u\n",
            static_cast<unsigned>(launched.error().domain),
            static_cast<unsigned>(launched.error().code));
    }
    assert(launched);
    const auto instance = launched.value();
    assert(instance.valid());

    const int data_fd = ::open(data_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(data_fd >= 0);

    // The child first asks its authenticated runtime session for a private
    // runtime→application input endpoint. No target identity or fd is provided
    // by the app in that request. Once the marker exists, App Manager owns the
    // opposite sender endpoint for this exact PeerIdentity.
    assert(wait_for_file(manager, data_fd, "m3-input-ready.bin"));

    os::app::ApplicationInputEventV1 input_event{
        .sequence = 41U,
        .target = instance.identity,
        .surface_id = 0x0000000900000001ULL,
        .frame_sequence = 17U,
        .surface_width_px = 360U,
        .surface_height_px = 800U,
        .local_x_px = 73,
        .local_y_px = 99,
        .pointer_id = 0U,
        .phase = os::app::ApplicationPointerPhase::down,
    };
    assert(input_event.valid());

    // Changing the target PeerIdentity cannot redirect the event to this
    // instance merely because it is the only running application.
    auto wrong_target = input_event;
    wrong_target.target.process = os::core::ProcessId{instance.identity.process.value() + 1U};
    auto wrong_delivery = manager.deliver_input_event(wrong_target);
    assert(!wrong_delivery);
    assert(wrong_delivery.error().domain == os::core::ErrorDomain::service);
    assert(wrong_delivery.error().code == os::app::manager_errors::input_target_not_found);

    assert(manager.deliver_input_event(input_event));

    // App Manager retains monotonic delivery state independently of endpoint
    // reacquisition; replay is rejected before another packet can be queued.
    auto replay = manager.deliver_input_event(input_event);
    assert(!replay);
    assert(replay.error().domain == os::core::ErrorDomain::service);
    assert(replay.error().code == os::app::manager_errors::input_event_replay);

    assert(wait_for_file(manager, data_fd, "m3-input-delivered.bin"));

    // Collection capability creation is bound to the same authenticated runtime
    // session. The application did not provide target identity, consumer
    // principal, session id, or native descriptor. The first manager-minted
    // session is deterministic in this fresh fixture.
    assert(wait_for_file(manager, data_fd, "m3-collection-ready.bin"));
    constexpr std::uint64_t collection_session = 1U;

    // Caller authorization precedes application/session lookup.
    auto denied_collection = manager.take_collection_endpoint(
        instance.identity,
        instance.identity,
        collection_session);
    assert(!denied_collection);
    assert(denied_collection.error().domain == os::core::ErrorDomain::service);
    assert(denied_collection.error().code == os::app::manager_errors::collection_authority_denied);

    auto wrong_collection = manager.take_collection_endpoint(
        collection_consumer_peer,
        instance.identity,
        collection_session + 1U);
    assert(!wrong_collection);
    assert(wrong_collection.error().domain == os::core::ErrorDomain::service);
    assert(wrong_collection.error().code == os::app::manager_errors::collection_endpoint_unavailable);

    auto collection_endpoint = manager.take_collection_endpoint(
        collection_consumer_peer,
        instance.identity,
        collection_session);
    assert(collection_endpoint);
    assert(collection_endpoint.value().valid());
    assert(collection_endpoint.value().application == instance.identity);
    assert(collection_endpoint.value().session_id == collection_session);

    os::collection::CollectionSessionClient collection_client{
        collection_endpoint.value().channel,
        os::collection::CollectionSessionId{collection_endpoint.value().session_id},
    };
    std::array<std::byte, os::ipc::max_wire_packet_size> collection_scratch{};
    auto collection_snapshot = collection_client.snapshot(collection_scratch);
    assert(collection_snapshot);
    assert(collection_snapshot.value().revision == os::ui::CollectionRevision{1U});
    assert(collection_snapshot.value().item_count == 1U);

    // App Manager transferred the consumer capability exactly once.
    auto replayed_collection_claim = manager.take_collection_endpoint(
        collection_consumer_peer,
        instance.identity,
        collection_session);
    assert(!replayed_collection_claim);
    assert(replayed_collection_claim.error().domain == os::core::ErrorDomain::service);
    assert(
        replayed_collection_claim.error().code ==
        os::app::manager_errors::collection_endpoint_unavailable);

    // Service the child's post-READY session requests until it has observed the
    // initial Storage and Key generations. This marker is written only after an
    // unauthorized ServiceId was denied and both generation observations were
    // returned over the long-lived runtime session.
    assert(wait_for_file(manager, data_fd, "m2-10-session-ready.bin"));

    const auto key_before = key_supervisor.status();
    assert(key_before.state == os::supervisor::ServiceState::running);
    assert(key_before.native_pid > 0 && key_before.generation != 0U);
    assert(::kill(key_before.native_pid, SIGKILL) == 0);

    // The app must observe its old KeyObject capability die, reacquire a fresh
    // Key Service endpoint over fd 3, reopen the same durable KeyId and decrypt
    // ciphertext created before the service crash.
    assert(wait_for_file(manager, data_fd, "m2-10-key-reacquired.bin"));
    const auto key_after = key_supervisor.status();
    assert(key_after.state == os::supervisor::ServiceState::running);
    assert(key_after.generation > key_before.generation);

    auto key_identity = key_supervisor.lookup_process(instance.native_pid);
    auto broker_identity = broker.lookup(instance.identity.process);
    auto authority_identity = process_authority.lookup(instance.native_pid);
    assert(key_identity && broker_identity && authority_identity);
    assert(key_identity.value().peer == instance.identity);
    assert(broker_identity.value().peer == instance.identity);
    assert(authority_identity.value().peer == instance.identity);

    const auto storage_before = storage_supervisor.status();
    assert(storage_before.state == os::supervisor::ServiceState::running);
    assert(storage_before.native_pid > 0 && storage_before.generation != 0U);
    assert(::kill(storage_before.native_pid, SIGKILL) == 0);

    // Storage follows the same rule: old root/object capabilities remain dead;
    // the long-lived runtime session yields a new main endpoint only after the
    // fresh service generation is running and profile policy is republished.
    assert(wait_for_file(manager, data_fd, "m2-10-storage-reacquired.bin"));
    const auto storage_after = storage_supervisor.status();
    assert(storage_after.state == os::supervisor::ServiceState::running);
    assert(storage_after.generation > storage_before.generation);

    auto storage_identity = storage_supervisor.lookup_process(instance.native_pid);
    broker_identity = broker.lookup(instance.identity.process);
    authority_identity = process_authority.lookup(instance.native_pid);
    assert(storage_identity && broker_identity && authority_identity);
    assert(storage_identity.value().peer == instance.identity);
    assert(broker_identity.value().peer == instance.identity);
    assert(authority_identity.value().peer == instance.identity);

    assert(manager.terminate(instance.instance, SIGTERM));
    assert(wait_until_gone(manager, instance.instance));
    assert(broker.process_count() == 0U);

    // uninstall_application collapses several stages - the durable
    // no-active-generation commit, profile revocation against Storage and
    // Keys, identity release, child teardown and a final maintain() - into one
    // first_error. This assertion has failed intermittently in CI and a bare
    // bool says nothing about which stage. Report before asserting.
    auto uninstalled = manager.uninstall_application(application);
    if (!uninstalled) {
        std::fprintf(
            stderr,
            "m2.10 uninstall error domain=%u code=%u\n",
            static_cast<unsigned>(uninstalled.error().domain),
            static_cast<unsigned>(uninstalled.error().code));
    }
    assert(uninstalled);
    assert(::close(data_fd) == 0);
    key_state_directory.reset();

    const int cleanup_data = ::open(data_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int cleanup_keys = ::open(key_state_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int cleanup_packages = ::open(package_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int cleanup_principals = ::open(principal_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(cleanup_data >= 0 && cleanup_keys >= 0 && cleanup_packages >= 0 && cleanup_principals >= 0);

    unlink_if_present(cleanup_data, "m2-10-initial.bin");
    unlink_if_present(cleanup_data, "m3-input-ready.bin");
    unlink_if_present(cleanup_data, "m3-input-delivered.bin");
    unlink_if_present(cleanup_data, "m3-collection-ready.bin");
    unlink_if_present(cleanup_data, "m2-10-session-ready.bin");
    unlink_if_present(cleanup_data, "m2-10-key-reacquired.bin");
    unlink_if_present(cleanup_data, "m2-10-storage-heartbeat.bin");
    unlink_if_present(cleanup_data, "m2-10-storage-reacquired.bin");
    cleanup_crash_left_atomic_temps(cleanup_data);
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