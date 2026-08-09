#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
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

constexpr os::core::PrincipalId target_application_principal{
    0x4150500000000000ULL,
    0x0000000000002201ULL,
};

constexpr os::core::UserId target_user{42U};

[[nodiscard]] bool write_signal(int fd) noexcept {
    const std::byte marker{0x51};
    for (;;) {
        const auto result = ::write(fd, &marker, 1U);
        if (result == 1) return true;
        if (result < 0 && errno == EINTR) continue;
        return false;
    }
}

[[nodiscard]] bool read_signal(int fd) noexcept {
    std::byte marker{};
    for (;;) {
        const auto result = ::read(fd, &marker, 1U);
        if (result == 1) return marker == std::byte{0x51};
        if (result < 0 && errno == EINTR) continue;
        return false;
    }
}

[[noreturn]] void run_target(
    os::ipc::Channel service_channel,
    int start_fd,
    int root_fd) {
    if (!read_signal(start_fd)) std::_Exit(40);

    os::ipc::ClientConnection connection{service_channel};

    // Even with an inherited directory descriptor, an ordinary application
    // principal cannot publish root policy into system.storage.
    os::storage::StorageProvisioner unauthorized{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    const os::core::NativeHandle inherited_root{root_fd};
    auto denied = unauthorized.provision_private_root(
        target_application_principal,
        target_user,
        inherited_root,
        scratch);
    if (denied || denied.error().domain != os::core::ErrorDomain::storage ||
        denied.error().code != os::storage::errors::access_denied) {
        std::_Exit(41);
    }

    os::storage::StorageClient client{connection};
    auto root_result = client.open_private_root(scratch);
    if (!root_result) std::_Exit(42);
    auto root = std::move(root_result).value();

    auto note_path = os::storage::RelativePath::parse("supervised.txt");
    if (!note_path) std::_Exit(43);

    constexpr std::array<std::byte, 10U> contents{
        std::byte{'s'}, std::byte{'u'}, std::byte{'p'}, std::byte{'e'}, std::byte{'r'},
        std::byte{'v'}, std::byte{'i'}, std::byte{'s'}, std::byte{'e'}, std::byte{'d'},
    };
    auto replaced = root.atomic_replace(note_path.value(), contents, scratch);
    if (!replaced) std::_Exit(44);

    os::storage::OpenOptions read_options{
        .access = os::storage::FileAccess::read_only,
        .create = os::storage::CreateDisposition::open_existing,
        .truncate = false,
    };
    auto file_result = root.open_file(note_path.value(), read_options, scratch);
    if (!file_result) std::_Exit(45);
    auto file = std::move(file_result).value();

    std::array<std::byte, contents.size()> output{};
    auto read = file.read_at(0U, output, scratch);
    if (!read || read.value() != contents.size() || output != contents) {
        std::_Exit(46);
    }

    std::_Exit(0);
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

    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = argv[1],
        },
    };
    auto started = supervisor.start();
    assert(started);
    assert(supervisor.status().state == os::supervisor::ServiceState::running);

    auto admin_record = supervisor.register_process(
        ::getpid(),
        os::storage::storage_profile_admin_principal,
        os::core::UserId{0U});
    assert(admin_record);

    char root_template[] = "/tmp/emnl-supervised-storage-XXXXXX";
    char* root_path = ::mkdtemp(root_template);
    assert(root_path != nullptr);
    const int native_root_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(native_root_fd >= 0);
    os::core::NativeHandle root_handle{native_root_fd};

    auto admin_channel_result = supervisor.connect();
    assert(admin_channel_result);
    auto admin_channel = std::move(admin_channel_result).value();
    os::ipc::ClientConnection admin_connection{admin_channel};
    os::storage::StorageProvisioner provisioner{admin_connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> admin_scratch{};

    auto provisioned = provisioner.provision_private_root(
        target_application_principal,
        target_user,
        root_handle,
        admin_scratch);
    assert(provisioned);

    // Re-publishing the same durable app/user profile is idempotent for this
    // service generation and does not retarget existing policy.
    provisioned = provisioner.provision_private_root(
        target_application_principal,
        target_user,
        root_handle,
        admin_scratch);
    assert(provisioned);

    auto target_channel_result = supervisor.connect();
    assert(target_channel_result);
    auto target_channel = std::move(target_channel_result).value();

    int start_pipe[2]{-1, -1};
    assert(::pipe2(start_pipe, O_CLOEXEC) == 0);

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        (void)::close(start_pipe[1]);
        admin_channel.close();
        run_target(std::move(target_channel), start_pipe[0], root_handle.native());
    }

    (void)::close(start_pipe[0]);
    target_channel.close();

    auto target_record = supervisor.register_process(
        child,
        target_application_principal,
        target_user);
    assert(target_record);
    assert(write_signal(start_pipe[1]));
    (void)::close(start_pipe[1]);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    auto unregistered = supervisor.unregister_process(target_record.value().peer.process);
    assert(unregistered);
    unregistered = supervisor.unregister_process(admin_record.value().peer.process);
    assert(unregistered);

    char file_path[512]{};
    const int length = std::snprintf(
        file_path, sizeof(file_path), "%s/supervised.txt", root_path);
    assert(length > 0 && static_cast<std::size_t>(length) < sizeof(file_path));
    assert(::unlink(file_path) == 0);
    root_handle.reset();
    assert(::rmdir(root_path) == 0);
    return 0;
}
