#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <time.h>
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

constexpr os::core::PrincipalId storage_service_principal{
    0x53595354454D0000ULL,
    0x000000000000F020ULL,
};

constexpr os::core::PrincipalId application_principal{
    0x4150500000000000ULL,
    0x0000000000003301ULL,
};

constexpr os::core::UserId application_user{77U};

void sleep_ms(long milliseconds) {
    const timespec delay{
        .tv_sec = milliseconds / 1000L,
        .tv_nsec = (milliseconds % 1000L) * 1'000'000L,
    };
    (void)::nanosleep(&delay, nullptr);
}

bool wait_for_running(os::supervisor::Supervisor& supervisor, os::core::ProcessId previous) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        auto maintained = supervisor.maintain();
        if (!maintained && supervisor.status().state == os::supervisor::ServiceState::crash_loop) {
            return false;
        }
        const auto status = supervisor.status();
        if (status.state == os::supervisor::ServiceState::running &&
            status.identity.process.value() != 0U &&
            status.identity.process != previous) {
            return true;
        }
        sleep_ms(5L);
    }
    return false;
}

os::storage::DirectoryObjectHandle open_root(os::supervisor::Supervisor& supervisor) {
    auto channel_result = supervisor.connect();
    assert(channel_result);
    auto channel = std::move(channel_result).value();
    os::ipc::ClientConnection connection{channel};
    os::storage::StorageClient client{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto root = client.open_private_root(scratch);
    assert(root);
    return std::move(root).value();
}

void provision(
    os::supervisor::Supervisor& supervisor,
    const os::core::NativeHandle& root_handle) {
    auto channel_result = supervisor.connect();
    assert(channel_result);
    auto channel = std::move(channel_result).value();
    os::ipc::ClientConnection connection{channel};
    os::storage::StorageProvisioner provisioner{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto result = provisioner.provision_private_root(
        application_principal,
        application_user,
        root_handle,
        scratch);
    assert(result);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    const os::supervisor::ServiceDescriptorV1 descriptor{
        .service_id = os::storage::storage_service_id,
        .principal_id = storage_service_principal,
        .user_id = os::core::UserId{0U},
        .name = "system.storage",
        .restart_policy = os::supervisor::RestartPolicy::on_failure,
        .restart_delay_ms = 10U,
        .max_restarts_in_window = 4U,
        .restart_window_ms = 5000U,
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

    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = argv[1],
        },
    };
    assert(supervisor.start());
    const auto first_service = supervisor.status();
    assert(first_service.state == os::supervisor::ServiceState::running);

    char root_template[] = "/tmp/emnl-storage-restart-XXXXXX";
    char* root_path = ::mkdtemp(root_template);
    assert(root_path != nullptr);
    const int root_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root_fd >= 0);
    os::core::NativeHandle root_handle{root_fd};

    auto admin = supervisor.register_process(
        ::getpid(),
        os::storage::storage_profile_admin_principal,
        os::core::UserId{0U});
    assert(admin);
    provision(supervisor, root_handle);
    assert(supervisor.unregister_process(admin.value().peer.process));

    auto app = supervisor.register_process(
        ::getpid(), application_principal, application_user);
    assert(app);
    auto old_root = open_root(supervisor);

    auto live_path = os::storage::RelativePath::parse("before-restart");
    assert(live_path);
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto created = old_root.create_directory(live_path.value(), scratch);
    assert(created);

    assert(supervisor.terminate(SIGKILL));
    sleep_ms(10L);

    auto stale_path = os::storage::RelativePath::parse("stale-endpoint");
    assert(stale_path);
    auto stale = old_root.create_directory(stale_path.value(), scratch);
    assert(!stale);
    assert(stale.error().domain == os::core::ErrorDomain::ipc);
    assert(stale.error().code == os::ipc::errors::peer_died);

    assert(wait_for_running(supervisor, first_service.identity.process));
    const auto second_service = supervisor.status();
    assert(second_service.generation == first_service.generation + 1U);

    // Supervisor republishes the still-live application process identity, but
    // Storage Service deliberately does not persist or resurrect its root
    // registry or object endpoints across a service generation.
    auto post_restart_channel = supervisor.connect();
    assert(post_restart_channel);
    auto channel = std::move(post_restart_channel).value();
    os::ipc::ClientConnection connection{channel};
    os::storage::StorageClient client{connection};
    auto missing = client.open_private_root(scratch);
    assert(!missing);
    assert(missing.error().domain == os::core::ErrorDomain::storage);
    assert(missing.error().code == os::storage::errors::root_not_registered);

    assert(supervisor.unregister_process(app.value().peer.process));
    admin = supervisor.register_process(
        ::getpid(),
        os::storage::storage_profile_admin_principal,
        os::core::UserId{0U});
    assert(admin);
    provision(supervisor, root_handle);
    assert(supervisor.unregister_process(admin.value().peer.process));

    app = supervisor.register_process(::getpid(), application_principal, application_user);
    assert(app);
    auto new_root = open_root(supervisor);
    auto after_path = os::storage::RelativePath::parse("after-restart");
    assert(after_path);
    created = new_root.create_directory(after_path.value(), scratch);
    assert(created);

    // The old generation remains permanently dead even after the same logical
    // storage profile is republished to the restarted service.
    stale = old_root.create_directory(stale_path.value(), scratch);
    assert(!stale);
    assert(stale.error().domain == os::core::ErrorDomain::ipc);
    assert(stale.error().code == os::ipc::errors::peer_died);

    assert(supervisor.unregister_process(app.value().peer.process));
    assert(::rmdir((std::string(root_path) + "/before-restart").c_str()) == 0);
    assert(::rmdir((std::string(root_path) + "/after-restart").c_str()) == 0);
    root_handle.reset();
    assert(::rmdir(root_path) == 0);
    return 0;
}
