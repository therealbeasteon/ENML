#include <cassert>
#include <cstdlib>

#include <os/supervisor/supervisor.hpp>

int main(int argc, char** argv) {
    assert(argc == 2);
    assert(::setenv("EMNL_SANDBOX_SECRET", "must-not-cross-exec", 1) == 0);

    const os::supervisor::ServiceDescriptorV1 descriptor{
        .service_id = os::core::ServiceId{0xFFFF00E8U},
        .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x00000000000000E8ULL},
        .user_id = os::core::UserId{0U},
        .name = "sandbox.probe",
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
            .max_open_files = 32U,
            .max_processes = 8U,
            .max_file_size_bytes = 1024U * 1024U,
        },
    };

    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = argv[1],
        }
    };

    auto start_result = supervisor.start();
    assert(start_result);
    const auto status = supervisor.status();
    assert(status.state == os::supervisor::ServiceState::running);
    assert(status.native_pid > 0);
    return 0;
}
