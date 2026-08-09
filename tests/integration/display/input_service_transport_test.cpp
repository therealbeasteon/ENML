#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/display/error.hpp>
#include <os/display/service.hpp>
#include <os/ipc/constants.hpp>

namespace {

constexpr std::uint64_t generation = 9U;

constexpr os::core::PrincipalId app_principal{
    0x494E505554415050ULL,
    0x0000000000000001ULL,
};
constexpr os::core::PrincipalId shell_principal{
    0x494E50555453484CULL,
    0x0000000000000001ULL,
};
constexpr os::core::PrincipalId secure_principal{
    0x494E505554534543ULL,
    0x0000000000000001ULL,
};

constexpr os::core::PeerIdentity app_owner{
    .principal = app_principal,
    .user = os::core::UserId{41U},
    .process = os::core::ProcessId{4101U},
};

constexpr os::core::PeerIdentity trusted_input_peer{
    .principal = os::display::input_service_principal,
    .user = os::core::UserId{0U},
    .process = os::core::ProcessId{5101U},
};

constexpr os::core::PeerIdentity ordinary_peer{
    .principal = app_principal,
    .user = os::core::UserId{41U},
    .process = os::core::ProcessId{5102U},
};

class FixedResolver final : public os::ipc::PeerIdentityResolver {
public:
    FixedResolver(pid_t expected_native_pid, os::core::PeerIdentity peer) noexcept
        : expected_native_pid_(expected_native_pid), peer_(peer) {}

    [[nodiscard]] os::core::Result<os::core::PeerIdentity> resolve(
        os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id != static_cast<std::int64_t>(expected_native_pid_)) {
            return os::core::make_error(
                os::core::ErrorDomain::security,
                os::core::errors::security::unknown_process);
        }
        return peer_;
    }

private:
    pid_t expected_native_pid_ {0};
    os::core::PeerIdentity peer_ {};
};

[[nodiscard]] os::display::Compositor make_compositor() {
    return os::display::Compositor{
        os::display::DisplayConfiguration{
            .size = {360U, 800U},
            .safe_insets = {},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        os::display::TrustedUiPrincipals{
            .shell = shell_principal,
            .secure_ui = secure_principal,
        },
        generation,
    };
}

[[nodiscard]] os::display::SurfaceDescriptor prepare_surface(
    os::display::Compositor& compositor) {
    auto created = compositor.create_surface(app_owner, {
        .role = os::display::SurfaceRole::application,
        .bounds = {40, 70, 220U, 300U},
        .accepts_input = true,
    });
    assert(created);
    assert(compositor.set_visibility(
        app_owner,
        created.value().id,
        os::display::SurfaceVisibility::visible));

    os::display::FrameSubmission frame{
        .surface = created.value().id,
        .buffer = os::display::BufferId{created.value().id.value() + 1000U},
        .sequence = 7U,
        .buffer_slot = 0U,
        .damage_count = 1U,
    };
    frame.damage[0] = {0, 0, 220U, 300U};
    assert(compositor.submit_frame(app_owner, frame, 1'000'000U));
    return created.value();
}

void run_authorized_transport_test() {
    auto compositor = make_compositor();
    assert(compositor.valid());
    const auto surface = prepare_surface(compositor);
    os::display::SharedBufferPool buffers{generation};
    assert(buffers.valid());

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const pid_t caller_pid = ::getpid();
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        FixedResolver resolver{caller_pid, trusted_input_peer};
        os::display::CompositorService service{
            compositor,
            buffers,
            resolver,
            os::display::input_service_principal,
        };
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto first = service.dispatch_once(pair[1], scratch);
        if (!first) ::_exit(20);
        auto second = service.dispatch_once(pair[1], scratch);
        if (!second) ::_exit(21);
        ::_exit(0);
    }

    pair[1].close();
    os::ipc::ClientConnection connection{pair[0]};
    os::display::InputCompositorClient input{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto hit = input.hit_test(100, 120, scratch);
    assert(hit);
    assert(hit.value().surface == surface.id);
    assert(hit.value().owner == app_owner);
    assert(hit.value().role == os::display::SurfaceRole::application);
    assert(hit.value().surface_size.width == 220U);
    assert(hit.value().surface_size.height == 300U);
    assert(hit.value().frame_sequence == 7U);
    assert(hit.value().local_x == 60);
    assert(hit.value().local_y == 50);
    assert(hit.value().trusted_presentation == os::display::TrustedPresentation::none);
    assert(input.validate_before_delivery(hit.value(), scratch));

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

void run_denied_transport_test() {
    auto compositor = make_compositor();
    assert(compositor.valid());
    static_cast<void>(prepare_surface(compositor));
    os::display::SharedBufferPool buffers{generation};
    assert(buffers.valid());

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const pid_t caller_pid = ::getpid();
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        FixedResolver resolver{caller_pid, ordinary_peer};
        os::display::CompositorService service{
            compositor,
            buffers,
            resolver,
            os::display::input_service_principal,
        };
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto dispatched = service.dispatch_once(pair[1], scratch);
        if (!dispatched) ::_exit(30);
        ::_exit(0);
    }

    pair[1].close();
    os::ipc::ClientConnection connection{pair[0]};
    os::display::InputCompositorClient input{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto denied = input.hit_test(100, 120, scratch);
    assert(!denied);
    assert(denied.error().domain == os::core::ErrorDomain::display);
    assert(denied.error().code == os::display::errors::input_authority_denied);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

} // namespace

int main() {
    run_authorized_transport_test();
    run_denied_transport_test();
    return 0;
}
