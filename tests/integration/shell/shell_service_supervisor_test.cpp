#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <signal.h>
#include <unistd.h>

#include <os/app/shell_lifecycle_control.hpp>
#include <os/core/error.hpp>
#include <os/core/platform_principals.hpp>
#include <os/ipc/constants.hpp>
#include <os/shell/service.hpp>
#include <os/supervisor/supervisor.hpp>

namespace {

constexpr os::core::PrincipalId ordinary_service_principal{
    0x4F5244494E415259ULL,
    0x5345525649434501ULL,
};

class ShellIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    void expect(pid_t native_pid, os::core::PeerIdentity peer) noexcept {
        native_pid_ = native_pid;
        peer_ = peer;
    }

    [[nodiscard]] os::core::Result<os::core::PeerIdentity> resolve(
        os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (native_pid_ <= 0 ||
            credentials.process_id != static_cast<std::int64_t>(native_pid_) ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid()) ||
            !os::core::valid_peer_identity(peer_)) {
            return os::core::make_error(
                os::core::ErrorDomain::security,
                os::core::errors::security::credential_mismatch);
        }
        return peer_;
    }

private:
    pid_t native_pid_ {-1};
    os::core::PeerIdentity peer_ {};
};

struct LifecycleFixture final {
    std::uint32_t calls {0U};
    std::uint32_t compositor_calls {0U};
    os::ipc::Channel compositor_capability {};
};

[[nodiscard]] os::core::Result<os::app::ApplicationLifecycleSnapshot> lifecycle_snapshot(
    void* context) noexcept {
    auto* fixture = static_cast<LifecycleFixture*>(context);
    if (fixture == nullptr) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::invalid_request);
    }
    ++fixture->calls;
    os::app::ApplicationLifecycleSnapshot snapshot{};
    snapshot.revision = 41U;
    return snapshot;
}

[[nodiscard]] os::core::Result<os::core::NativeHandle> take_compositor_capability(
    void* context) noexcept {
    auto* fixture = static_cast<LifecycleFixture*>(context);
    if (fixture == nullptr || !fixture->compositor_capability.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::not_supported);
    }
    ++fixture->compositor_calls;
    return fixture->compositor_capability.take_native_handle_for_transfer();
}

os::supervisor::ServiceLaunchConfig shell_config(
    const char* executable,
    os::core::PrincipalId principal,
    int lifecycle_fd) noexcept {
    return os::supervisor::ServiceLaunchConfig{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = os::shell::shell_service_id,
            .principal_id = principal,
            .user_id = os::core::UserId{0U},
            .name = "system.shell",
            .restart_policy = os::supervisor::RestartPolicy::never,
            .restart_delay_ms = 10U,
            .max_restarts_in_window = 1U,
            .restart_window_ms = 1000U,
            .readiness_timeout_ms = 1000U,
            .sandbox = {},
        },
        .executable_path = executable,
        .private_capability_fd = lifecycle_fd,
    };
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    // A process launched under the shell ServiceId but with any other trusted
    // runtime principal must fail before READY. Numeric service labels alone do
    // not create shell authority.
    {
        auto pair_result = os::ipc::Channel::create_local_pair();
        assert(pair_result);
        auto pair = std::move(pair_result).value();
        os::supervisor::ProcessAuthority authority;
        os::supervisor::Supervisor wrong_principal{
            shell_config(argv[1], ordinary_service_principal, pair[1].native_fd()),
            authority,
        };
        assert(!wrong_principal.start());
    }

    // The canonical shell principal without its private lifecycle capability is
    // also not ready. Startup fails closed instead of falling back to public
    // process enumeration, a task scanner or ambient App Manager access.
    {
        os::supervisor::ProcessAuthority authority;
        os::supervisor::Supervisor missing_capability{
            shell_config(argv[1], os::core::shell_service_principal, -1),
            authority,
        };
        assert(!missing_capability.start());
    }

    auto lifecycle_pair_result = os::ipc::Channel::create_local_pair();
    assert(lifecycle_pair_result);
    auto lifecycle_pair = std::move(lifecycle_pair_result).value();

    os::supervisor::ProcessAuthority authority;
    os::supervisor::Supervisor supervisor{
        shell_config(
            argv[1],
            os::core::shell_service_principal,
            lifecycle_pair[1].native_fd()),
        authority,
    };
    assert(supervisor.start());
    assert(supervisor.status().state == os::supervisor::ServiceState::running);
    assert(supervisor.status().identity.principal == os::core::shell_service_principal);
    assert(supervisor.status().native_pid > 0);

    // The Supervisor has already duplicated the private endpoint into the shell
    // at fd 6. Drop the parent's source endpoint so only the real shell process
    // remains on the client side of this bootstrap authority channel.
    lifecycle_pair[1].close();

    auto compositor_pair_result = os::ipc::Channel::create_local_pair();
    assert(compositor_pair_result);
    auto compositor_pair = std::move(compositor_pair_result).value();

    ShellIdentityResolver resolver{};
    resolver.expect(supervisor.status().native_pid, supervisor.status().identity);
    LifecycleFixture lifecycle{};
    lifecycle.compositor_capability = std::move(compositor_pair[1]);
    const os::app::ShellLifecycleBackend backend{
        .context = &lifecycle,
        .snapshot = lifecycle_snapshot,
        .compositor_context = &lifecycle,
        .take_compositor_capability = take_compositor_capability,
    };
    os::app::ShellLifecycleControlServer server{backend, resolver};
    assert(server.valid());

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto dispatched = server.dispatch_once(lifecycle_pair[0], scratch);
    assert(dispatched);
    assert(lifecycle.calls == 1U);

    // The same authenticated bootstrap channel transfers one move-only private
    // compositor-control endpoint. A second Supervisor-private fd is not needed,
    // and the shell still does not receive a public compositor-admin endpoint.
    dispatched = server.dispatch_once(lifecycle_pair[0], scratch);
    assert(dispatched);
    assert(lifecycle.compositor_calls == 1U);
    assert(!lifecycle.compositor_capability.valid());
    assert(compositor_pair[0].valid());

    // Let the shell consume both replies and enter its blocking trusted control
    // loop. If either semantic snapshot decoding or compositor-handle adoption
    // failed, the process exits and maintain() observes that failure here.
    ::usleep(20'000U);
    assert(supervisor.maintain());
    assert(supervisor.status().state == os::supervisor::ServiceState::running);

    assert(supervisor.terminate(SIGTERM));
    return 0;
}
