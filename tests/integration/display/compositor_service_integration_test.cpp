#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <signal.h>
#include <unistd.h>

#include <os/display/error.hpp>
#include <os/display/service.hpp>
#include <os/ipc/constants.hpp>
#include <os/supervisor/service_broker.hpp>
#include <os/supervisor/supervisor.hpp>

namespace {

constexpr os::core::PrincipalId compositor_principal{
    0x4D33434F4D504F53ULL,
    0x49544F5200000001ULL,
};
constexpr os::core::PrincipalId app_principal{
    0x4D334150504C4943ULL,
    0x4154494F4E000001ULL,
};
constexpr os::core::UserId app_user{73U};

os::supervisor::ServiceLaunchConfig make_config(const char* executable) {
    os::sandbox::SandboxPolicyV1 sandbox{};
    // Shared pixel buffers are bounded by osdisplay to 24 MiB each. The
    // compositor therefore needs a larger per-file ceiling than the generic
    // 1 MiB service default, while the semantic pool still enforces the much
    // tighter per-principal/global memory budgets.
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
    };
}

void wait_for_new_generation(
    os::supervisor::Supervisor& supervisor,
    std::uint64_t prior_generation) {
    for (std::size_t attempt = 0U; attempt < 400U; ++attempt) {
        auto maintained = supervisor.maintain();
        assert(maintained);
        const auto status = supervisor.status();
        if (status.state == os::supervisor::ServiceState::running &&
            status.generation > prior_generation) {
            return;
        }
        ::usleep(5000U);
    }
    assert(false);
}

void expect_peer_died(const os::core::Error& error) {
    assert(error.domain == os::core::ErrorDomain::ipc);
    assert(error.code == os::ipc::errors::peer_died);
}

void expect_display_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    os::supervisor::ProcessAuthority authority;
    os::supervisor::Supervisor supervisor{make_config(argv[1]), authority};
    assert(supervisor.start());
    assert(supervisor.status().state == os::supervisor::ServiceState::running);

    os::supervisor::ServiceBroker broker{authority};
    assert(broker.register_service(os::display::compositor_service_id, supervisor));
    const std::array requested_services{os::display::compositor_service_id};
    auto attached = broker.attach_process(
        ::getpid(),
        app_principal,
        app_user,
        std::span<const os::core::ServiceId>{requested_services});
    assert(attached);
    const auto process = attached.value().peer.process;

    const std::uint64_t first_generation = supervisor.status().generation;
    assert(os::display::valid_display_generation(first_generation));

    auto first_channel_result = broker.connect(process, os::display::compositor_service_id);
    assert(first_channel_result);
    auto first_channel = std::move(first_channel_result).value();
    os::ipc::ClientConnection first_connection{first_channel};
    os::display::CompositorClient first_client{first_connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto configuration = first_client.configuration(scratch);
    assert(configuration);
    auto first_surface = first_client.create_surface({
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 64U, 64U},
        .accepts_input = true,
    }, scratch);
    assert(first_surface);
    assert(os::display::display_object_generation(first_surface.value().id.value()) ==
        static_cast<std::uint32_t>(first_generation));

    auto first_buffer_result = first_client.allocate_buffer(
        {64U, 64U}, os::display::PixelFormat::rgba8888, scratch);
    assert(first_buffer_result);
    auto first_buffer = std::move(first_buffer_result).value();
    assert(os::display::display_object_generation(first_buffer.descriptor.id.value()) ==
        static_cast<std::uint32_t>(first_generation));

    os::display::FrameSubmission first_frame{
        .surface = first_surface.value().id,
        .buffer = first_buffer.descriptor.id,
        .sequence = 1U,
        .buffer_slot = 0U,
        .damage_count = 1U,
    };
    first_frame.damage[0] = {0, 0, 64U, 64U};
    auto first_receipt = first_client.submit_frame(first_frame, scratch);
    assert(first_receipt);
    assert(first_receipt.value().surface == first_surface.value().id);
    assert(first_receipt.value().buffer == first_buffer.descriptor.id);

    // Crash the exact managed compositor process. The Supervisor must publish a
    // fresh service generation while the application's boot-scoped PeerIdentity
    // remains attached through the broker.
    assert(supervisor.terminate(SIGKILL));
    wait_for_new_generation(supervisor, first_generation);
    const std::uint64_t second_generation = supervisor.status().generation;
    assert(second_generation > first_generation);
    assert(os::display::valid_display_generation(second_generation));

    // A generation-bound endpoint never silently reconnects to the replacement
    // compositor. Old transport authority remains stale permanently.
    auto stale_configuration = first_client.configuration(scratch);
    assert(!stale_configuration);
    expect_peer_died(stale_configuration.error());

    auto second_channel_result = broker.connect(process, os::display::compositor_service_id);
    assert(second_channel_result);
    auto second_channel = std::move(second_channel_result).value();
    os::ipc::ClientConnection second_connection{second_channel};
    os::display::CompositorClient second_client{second_connection};

    auto second_surface = second_client.create_surface({
        .role = os::display::SurfaceRole::application,
        .bounds = {0, 0, 64U, 64U},
        .accepts_input = true,
    }, scratch);
    assert(second_surface);
    auto second_buffer_result = second_client.allocate_buffer(
        {64U, 64U}, os::display::PixelFormat::rgba8888, scratch);
    assert(second_buffer_result);
    auto second_buffer = std::move(second_buffer_result).value();

    assert(first_surface.value().id != second_surface.value().id);
    assert(first_buffer.descriptor.id != second_buffer.descriptor.id);
    assert(os::display::display_object_generation(second_surface.value().id.value()) ==
        static_cast<std::uint32_t>(second_generation));
    assert(os::display::display_object_generation(second_buffer.descriptor.id.value()) ==
        static_cast<std::uint32_t>(second_generation));
    assert(os::display::display_object_serial(first_surface.value().id.value()) ==
        os::display::display_object_serial(second_surface.value().id.value()));
    assert(os::display::display_object_serial(first_buffer.descriptor.id.value()) ==
        os::display::display_object_serial(second_buffer.descriptor.id.value()));

    // Numeric local serial reuse is expected; the service generation namespace
    // prevents ABA. A stale semantic id cannot address a replacement object.
    auto stale_surface = second_client.destroy_surface(first_surface.value().id, scratch);
    assert(!stale_surface);
    expect_display_error(stale_surface.error(), os::display::errors::unknown_surface);

    auto stale_buffer = second_client.release_buffer(first_buffer.descriptor.id, scratch);
    assert(!stale_buffer);
    expect_display_error(stale_buffer.error(), os::display::errors::invalid_buffer);

    os::display::FrameSubmission second_frame{
        .surface = second_surface.value().id,
        .buffer = second_buffer.descriptor.id,
        .sequence = 1U,
        .buffer_slot = 0U,
        .damage_count = 1U,
    };
    second_frame.damage[0] = {0, 0, 64U, 64U};
    assert(second_client.submit_frame(second_frame, scratch));

    assert(broker.detach_process(process));
    return 0;
}
