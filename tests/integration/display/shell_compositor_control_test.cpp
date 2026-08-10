#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/platform_principals.hpp>
#include <os/display/error.hpp>
#include <os/display/shell_control.hpp>
#include <os/ipc/constants.hpp>

namespace {

constexpr os::core::PrincipalId app_one_principal{0x4150503100000001ULL, 1U};
constexpr os::core::PrincipalId app_two_principal{0x4150503200000001ULL, 2U};
constexpr os::core::PrincipalId ordinary_principal{0x4F5244494E415259ULL, 3U};
constexpr os::core::PeerIdentity app_one{
    app_one_principal,
    os::core::UserId{7U},
    os::core::ProcessId{101U},
};
constexpr os::core::PeerIdentity app_two{
    app_two_principal,
    os::core::UserId{7U},
    os::core::ProcessId{201U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
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

os::core::PeerIdentity service_peer(os::core::PrincipalId principal, std::uint64_t process) {
    return os::core::PeerIdentity{
        .principal = principal,
        .user = os::core::UserId{0U},
        .process = os::core::ProcessId{process},
    };
}

os::display::Compositor make_compositor() {
    return os::display::Compositor{
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .safe_insets = {.top = 80U, .bottom = 100U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        os::display::TrustedUiPrincipals{
            .shell = os::core::shell_service_principal,
            .secure_ui = os::core::secure_ui_service_principal,
        },
        12U,
    };
}

os::display::SurfaceDescriptor create_application(
    os::display::Compositor& compositor,
    os::core::PeerIdentity owner) {
    auto created = compositor.create_surface(owner, {
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 1080U, 2400U},
        .accepts_input = true,
    });
    assert(created);
    return created.value();
}

} // namespace

int main() {
    auto compositor = make_compositor();
    assert(compositor.valid());
    const auto first = create_application(compositor, app_one);
    const auto second = create_application(compositor, app_two);

    TestIdentityResolver resolver{};
    os::display::ShellCompositorControlServer server{
        os::display::shell_compositor_backend(compositor),
        resolver,
    };
    assert(server.valid());

    // Exact authenticated shell activation succeeds and changes only stack
    // order; application identity/ownership remains compositor-authored.
    auto trusted_pair_result = os::ipc::Channel::create_local_pair();
    assert(trusted_pair_result);
    auto trusted_pair = std::move(trusted_pair_result).value();
    const pid_t trusted_child = ::fork();
    assert(trusted_child >= 0);
    if (trusted_child == 0) {
        trusted_pair[0].close();
        os::display::ShellCompositorControlClient client{trusted_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto activated = client.activate_exact(app_one, first.id, scratch);
        if (!activated) ::_exit(20);
        ::_exit(0);
    }
    trusted_pair[1].close();
    resolver.expect(trusted_child, service_peer(os::core::shell_service_principal, 9001U));
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    assert(server.dispatch_once(trusted_pair[0], scratch));
    int trusted_status = 0;
    assert(::waitpid(trusted_child, &trusted_status, 0) == trusted_child);
    assert(WIFEXITED(trusted_status));
    assert(WEXITSTATUS(trusted_status) == 0);
    auto scene = compositor.scene_snapshot();
    assert(scene.count == 2U);
    assert(scene.entries[0].surface.id == second.id);
    assert(scene.entries[1].surface.id == first.id);

    // Endpoint possession is not authority. The ordinary sender is denied
    // before its owner/surface payload is interpreted.
    auto denied_pair_result = os::ipc::Channel::create_local_pair();
    assert(denied_pair_result);
    auto denied_pair = std::move(denied_pair_result).value();
    const pid_t ordinary_child = ::fork();
    assert(ordinary_child >= 0);
    if (ordinary_child == 0) {
        denied_pair[0].close();
        os::display::ShellCompositorControlClient client{denied_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> child_scratch{};
        auto denied = client.activate_exact(app_two, second.id, child_scratch);
        if (denied || denied.error().domain != os::core::ErrorDomain::service ||
            denied.error().code != os::core::errors::service::access_denied) {
            ::_exit(30);
        }
        ::_exit(0);
    }
    denied_pair[1].close();
    resolver.expect(ordinary_child, service_peer(ordinary_principal, 9002U));
    assert(server.dispatch_once(denied_pair[0], scratch));
    int ordinary_status = 0;
    assert(::waitpid(ordinary_child, &ordinary_status, 0) == ordinary_child);
    assert(WIFEXITED(ordinary_status));
    assert(WEXITSTATUS(ordinary_status) == 0);

    // A trusted shell request with the wrong exact lifecycle owner fails at the
    // compositor commit point. It cannot redirect the app-one task identity to
    // app-two's surface.
    auto mismatch_pair_result = os::ipc::Channel::create_local_pair();
    assert(mismatch_pair_result);
    auto mismatch_pair = std::move(mismatch_pair_result).value();
    const pid_t mismatch_child = ::fork();
    assert(mismatch_child >= 0);
    if (mismatch_child == 0) {
        mismatch_pair[0].close();
        os::display::ShellCompositorControlClient client{mismatch_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> child_scratch{};
        auto mismatch = client.activate_exact(app_one, second.id, child_scratch);
        if (mismatch || mismatch.error().domain != os::core::ErrorDomain::display ||
            mismatch.error().code != os::display::errors::activation_denied) {
            ::_exit(40);
        }
        ::_exit(0);
    }
    mismatch_pair[1].close();
    resolver.expect(mismatch_child, service_peer(os::core::shell_service_principal, 9003U));
    assert(server.dispatch_once(mismatch_pair[0], scratch));
    int mismatch_status = 0;
    assert(::waitpid(mismatch_child, &mismatch_status, 0) == mismatch_child);
    assert(WIFEXITED(mismatch_status));
    assert(WEXITSTATUS(mismatch_status) == 0);

    // Generation-scoped stale ids stay stale across the private transport.
    auto stale_pair_result = os::ipc::Channel::create_local_pair();
    assert(stale_pair_result);
    auto stale_pair = std::move(stale_pair_result).value();
    const auto stale_surface = os::display::SurfaceId{
        os::display::make_display_object_value(11U, 1U)};
    const pid_t stale_child = ::fork();
    assert(stale_child >= 0);
    if (stale_child == 0) {
        stale_pair[0].close();
        os::display::ShellCompositorControlClient client{stale_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> child_scratch{};
        auto stale = client.activate_exact(app_one, stale_surface, child_scratch);
        if (stale || stale.error().domain != os::core::ErrorDomain::display ||
            stale.error().code != os::display::errors::unknown_surface) {
            ::_exit(50);
        }
        ::_exit(0);
    }
    stale_pair[1].close();
    resolver.expect(stale_child, service_peer(os::core::shell_service_principal, 9004U));
    assert(server.dispatch_once(stale_pair[0], scratch));
    int stale_status = 0;
    assert(::waitpid(stale_child, &stale_status, 0) == stale_child);
    assert(WIFEXITED(stale_status));
    assert(WEXITSTATUS(stale_status) == 0);

    return 0;
}
