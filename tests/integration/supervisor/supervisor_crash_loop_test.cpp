#include <cassert>

#include <signal.h>
#include <time.h>

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

void drive_until_not_waiting(os::supervisor::Supervisor& supervisor) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        (void)supervisor.maintain();
        const auto state = supervisor.status().state;
        if (state == os::supervisor::ServiceState::running ||
            state == os::supervisor::ServiceState::crash_loop ||
            state == os::supervisor::ServiceState::stopped) {
            return;
        }
        sleep_ms(2L);
    }
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
        .restart_delay_ms = 1U,
        .max_restarts_in_window = 1U,
        .restart_window_ms = 10000U,
        .readiness_timeout_ms = 1000U,
    };
    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = argv[1],
        }
    };

    assert(supervisor.start());
    assert(supervisor.terminate(SIGKILL));
    sleep_ms(5L);
    (void)supervisor.maintain();
    drive_until_not_waiting(supervisor);
    assert(supervisor.status().state == os::supervisor::ServiceState::running);
    assert(supervisor.status().generation == 2U);

    assert(supervisor.terminate(SIGKILL));
    sleep_ms(5L);
    auto result = supervisor.maintain();
    assert(!result);
    assert(result.error().domain == os::core::ErrorDomain::service);
    assert(result.error().code == os::core::errors::service::crash_loop);
    assert(supervisor.status().state == os::supervisor::ServiceState::crash_loop);
    return 0;
}
