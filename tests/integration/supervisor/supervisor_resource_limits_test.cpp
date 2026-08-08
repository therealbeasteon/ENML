#include <cassert>

#include <os/supervisor/supervisor.hpp>

int main(int argc, char** argv) {
    assert(argc == 2);

    const os::supervisor::ServiceDescriptorV1 descriptor{
        .service_id = os::core::ServiceId{0xFFFF0902U},
        .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x0000000000000902ULL},
        .user_id = os::core::UserId{0U},
        .name = "resource.probe",
        .restart_policy = os::supervisor::RestartPolicy::never,
        .restart_delay_ms = 10U,
        .max_restarts_in_window = 0U,
        .restart_window_ms = 1000U,
        .readiness_timeout_ms = 1000U,
        .sandbox = os::sandbox::SandboxPolicyV1{
            .enabled = true,
            .require_no_new_privs = true,
            .clear_capabilities = true,
            .require_seccomp = true,
            .require_landlock = false,
            .max_open_files = 12U,
            .max_processes = 8U,
            .max_file_size_bytes = 4096U,
        },
    };

    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = argv[1],
        }
    };

    auto result = supervisor.start();
    assert(result);
    assert(supervisor.status().state == os::supervisor::ServiceState::running);
    return 0;
}
