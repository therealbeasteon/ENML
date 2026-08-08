#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/supervisor/supervisor.hpp>

#include "echo_client.generated.hpp"

namespace generated = os::test::echo;

namespace {

void sleep_ms(long milliseconds) {
    const timespec delay{
        .tv_sec = milliseconds / 1000L,
        .tv_nsec = (milliseconds % 1000L) * 1'000'000L,
    };
    (void)::nanosleep(&delay, nullptr);
}

bool wait_for_running(os::supervisor::Supervisor& supervisor, os::core::ProcessId previous) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        auto maintain_result = supervisor.maintain();
        if (!maintain_result && supervisor.status().state == os::supervisor::ServiceState::crash_loop) {
            return false;
        }
        const auto status = supervisor.status();
        if (status.state == os::supervisor::ServiceState::running &&
            status.identity.process.value() != 0U && status.identity.process != previous) {
            return true;
        }
        sleep_ms(5L);
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    const os::supervisor::ServiceDescriptorV1 descriptor{
        .service_id = generated::EchoClient::service_id,
        .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x000000000000F001ULL},
        .user_id = os::core::UserId{0U},
        .name = "system.echo",
        .restart_policy = os::supervisor::RestartPolicy::on_failure,
        .restart_delay_ms = 10U,
        .max_restarts_in_window = 4U,
        .restart_window_ms = 5000U,
        .readiness_timeout_ms = 1000U,
    };
    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = argv[1],
        }
    };

    auto start_result = supervisor.start();
    assert(start_result);
    auto first_status = supervisor.status();
    assert(first_status.state == os::supervisor::ServiceState::running);
    assert(first_status.identity.process.value() != 0U);
    assert(first_status.native_pid > 0);
    assert(first_status.generation == 1U);

    constexpr os::core::PrincipalId client_principal{0x5445535400000000ULL, 0x0000000000000001ULL};
    constexpr os::core::UserId client_user{100U};
    auto registration_result = supervisor.register_process(::getpid(), client_principal, client_user);
    assert(registration_result);
    const auto client_identity = registration_result.value().peer;

    auto channel_result = supervisor.connect();
    assert(channel_result);
    auto channel = std::move(channel_result).value();
    os::ipc::ClientConnection connection{channel};
    generated::EchoClient client{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> response_buffer{};

    auto ping_result = client.ping(generated::PingRequest{.text = "before-restart"}, response_buffer);
    assert(ping_result);
    assert(ping_result.value().text == "before-restart");

    auto identify_result = client.identify(generated::Empty{}, response_buffer);
    assert(identify_result);
    assert(identify_result.value().process_id == client_identity.process.value());
    assert(identify_result.value().principal_high == client_principal.high);
    assert(identify_result.value().principal_low == client_principal.low);
    assert(identify_result.value().user_id == client_user.value());

    auto terminate_result = supervisor.terminate(SIGKILL);
    assert(terminate_result);

    sleep_ms(10L);
    auto dead_result = client.ping(generated::PingRequest{.text = "old-channel"}, response_buffer);
    assert(!dead_result);
    assert(dead_result.error().domain == os::core::ErrorDomain::ipc);
    assert(dead_result.error().code == os::ipc::errors::peer_died);

    assert(wait_for_running(supervisor, first_status.identity.process));
    const auto second_status = supervisor.status();
    assert(second_status.state == os::supervisor::ServiceState::running);
    assert(second_status.identity.process != first_status.identity.process);
    assert(second_status.generation == 2U);

    auto second_channel_result = supervisor.connect();
    assert(second_channel_result);
    auto second_channel = std::move(second_channel_result).value();
    os::ipc::ClientConnection second_connection{second_channel};
    generated::EchoClient second_client{second_connection};

    auto second_ping = second_client.ping(
        generated::PingRequest{.text = "after-restart"}, response_buffer);
    assert(second_ping);
    assert(second_ping.value().text == "after-restart");
    return 0;
}
