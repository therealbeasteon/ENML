#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/platform_principals.hpp>
#include <os/display/service.hpp>
#include <os/ipc/constants.hpp>
#include <os/shell/compositor_client.hpp>
#include <os/supervisor/service_broker.hpp>
#include <os/supervisor/supervisor.hpp>

namespace {

constexpr os::core::PrincipalId compositor_principal{
    0x4D3450524956434FULL,
    0x4D50000000000001ULL,
};
constexpr os::core::PrincipalId app_principal{
    0x4D34505249564150ULL,
    0x5000000000000001ULL,
};
constexpr os::core::UserId app_user{91U};

os::supervisor::ServiceLaunchConfig make_config(
    const char* executable,
    int shell_private_fd) noexcept {
    os::sandbox::SandboxPolicyV1 sandbox{};
    sandbox.max_file_size_bytes = 32ULL * 1024ULL * 1024ULL;
    return os::supervisor::ServiceLaunchConfig{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = os::display::compositor_service_id,
            .principal_id = compositor_principal,
            .user_id = os::core::UserId{0U},
            .name = "system.compositor",
            .restart_policy = os::supervisor::RestartPolicy::on_failure,
            .restart_delay_ms = 10U,
            .max_restarts_in_window = 3U,
            .restart_window_ms = 2000U,
            .readiness_timeout_ms = 2000U,
            .sandbox = sandbox,
        },
        .executable_path = executable,
        .private_capability_fd = shell_private_fd,
    };
}

void write_gate(int fd) {
    const std::byte value{0x5A};
    assert(::write(fd, &value, sizeof(value)) == static_cast<ssize_t>(sizeof(value)));
}

void read_gate(int fd) {
    std::byte value{};
    assert(::read(fd, &value, sizeof(value)) == static_cast<ssize_t>(sizeof(value)));
    assert(value == std::byte{0x5A});
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    auto shell_pair_result = os::ipc::Channel::create_local_pair();
    assert(shell_pair_result);
    auto shell_pair = std::move(shell_pair_result).value();

    os::supervisor::ProcessAuthority authority;
    os::supervisor::Supervisor compositor_supervisor{
        make_config(argv[1], shell_pair[0].native_fd()),
        authority,
    };
    assert(compositor_supervisor.start());
    assert(compositor_supervisor.status().state == os::supervisor::ServiceState::running);

    // Register this test process as an ordinary application through the public
    // broker path and create one exact application root. This identity is what
    // the private shell request must name at the compositor commit point.
    os::supervisor::ServiceBroker broker{authority};
    assert(broker.register_service(os::display::compositor_service_id, compositor_supervisor));
    const std::array requested_services{os::display::compositor_service_id};
    auto attached = broker.attach_process(
        ::getpid(),
        app_principal,
        app_user,
        std::span<const os::core::ServiceId>{requested_services});
    assert(attached);
    const auto app_peer = attached.value().peer;

    auto public_channel_result = broker.connect(
        app_peer.process,
        os::display::compositor_service_id);
    assert(public_channel_result);
    auto public_channel = std::move(public_channel_result).value();
    os::ipc::ClientConnection public_connection{public_channel};
    os::display::CompositorClient public_client{public_connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> public_scratch{};
    auto surface = public_client.create_surface({
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 64U, 64U},
        .accepts_input = true,
    }, public_scratch);
    assert(surface);

    int gate[2] {-1, -1};
    assert(::pipe(gate) == 0);
    const pid_t shell_child = ::fork();
    assert(shell_child >= 0);
    if (shell_child == 0) {
        (void)::close(gate[1]);
        shell_pair[0].close();
        public_channel.close();

        read_gate(gate[0]);
        (void)::close(gate[0]);

        os::shell::ShellCompositorClient shell_client{shell_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto activated = shell_client.activate_exact(
            app_peer,
            surface.value().id,
            scratch);
        if (!activated) ::_exit(20);
        ::_exit(0);
    }

    (void)::close(gate[0]);
    auto registered_shell = compositor_supervisor.register_process(
        shell_child,
        os::core::shell_service_principal,
        os::core::UserId{0U});
    assert(registered_shell);
    write_gate(gate[1]);
    (void)::close(gate[1]);

    int shell_status = 0;
    assert(::waitpid(shell_child, &shell_status, 0) == shell_child);
    assert(WIFEXITED(shell_status));
    assert(WEXITSTATUS(shell_status) == 0);

    // The parent still possesses the same private endpoint but is registered as
    // an application, not the shell. Production system.compositor must deny it
    // before interpreting the target owner/surface as privileged navigation.
    os::shell::ShellCompositorClient ordinary_client{shell_pair[1]};
    std::array<std::byte, os::ipc::max_wire_packet_size> private_scratch{};
    auto denied = ordinary_client.activate_exact(
        app_peer,
        surface.value().id,
        private_scratch);
    assert(!denied);
    assert(denied.error().domain == os::core::ErrorDomain::service);
    assert(denied.error().code == os::core::errors::service::access_denied);

    assert(broker.detach_process(app_peer.process));
    return 0;
}
