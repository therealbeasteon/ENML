#include <cassert>

#include <os/supervisor/supervisor.hpp>

#include "echo_client.generated.hpp"

namespace generated = os::test::echo;

int main(int argc, char** argv) {
    assert(argc == 2);

    const os::supervisor::ServiceDescriptorV1 descriptor{
        .service_id = generated::EchoClient::service_id,
        .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x000000000000F001ULL},
        .user_id = os::core::UserId{0U},
        .name = "never.ready",
        .restart_policy = os::supervisor::RestartPolicy::never,
        .restart_delay_ms = 1U,
        .max_restarts_in_window = 0U,
        .restart_window_ms = 1000U,
        .readiness_timeout_ms = 20U,
    };
    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = argv[1],
        }
    };

    auto result = supervisor.start();
    assert(!result);
    assert(result.error().domain == os::core::ErrorDomain::service);
    assert(result.error().code == os::core::errors::service::readiness_timeout);
    assert(supervisor.status().state == os::supervisor::ServiceState::stopped);
    assert(supervisor.status().native_pid == -1);
    return 0;
}
