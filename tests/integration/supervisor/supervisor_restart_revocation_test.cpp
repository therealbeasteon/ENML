#include <array>
#include <cassert>
#include <cstddef>

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

bool wait_for_state(os::supervisor::Supervisor& supervisor, os::supervisor::ServiceState target) {
    for (int attempt = 0; attempt < 400; ++attempt) {
        (void)supervisor.maintain();
        if (supervisor.status().state == target) return true;
        sleep_ms(2L);
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
        .restart_delay_ms = 75U,
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
    assert(supervisor.start());

    constexpr os::core::PrincipalId client_principal{
        0x5445535400000000ULL,
        0x0000000000000909ULL,
    };
    constexpr os::core::UserId client_user{909U};
    auto registration = supervisor.register_process(::getpid(), client_principal, client_user);
    assert(registration);
    const auto process_id = registration.value().peer.process;

    auto channel_result = supervisor.connect();
    assert(channel_result);
    auto channel = std::move(channel_result).value();
    os::ipc::ClientConnection connection{channel};
    generated::EchoClient client{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> response{};
    assert(client.ping(generated::PingRequest{.text = "authorized"}, response));

    assert(supervisor.terminate(SIGKILL));
    assert(wait_for_state(supervisor, os::supervisor::ServiceState::restart_wait));
    assert(supervisor.unregister_process(process_id));
    assert(wait_for_state(supervisor, os::supervisor::ServiceState::running));

    auto new_channel_result = supervisor.connect();
    assert(new_channel_result);
    auto new_channel = std::move(new_channel_result).value();
    os::ipc::ClientConnection new_connection{new_channel};
    generated::EchoClient new_client{new_connection};
    auto denied = new_client.ping(generated::PingRequest{.text = "must-stay-revoked"}, response);
    assert(!denied);
    assert(denied.error().domain == os::core::ErrorDomain::security);
    assert(denied.error().code == os::core::errors::security::unknown_process);
    return 0;
}
