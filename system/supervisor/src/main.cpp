#include <cstdio>
#include <cstring>

#include <signal.h>
#include <time.h>

#include <os/supervisor/supervisor.hpp>

namespace {

volatile sig_atomic_t keep_running = 1;

void stop_handler(int) {
    keep_running = 0;
}

constexpr os::supervisor::ServiceDescriptorV1 echo_descriptor{
    .service_id = os::core::ServiceId{0x0000F001U},
    .principal_id = os::core::PrincipalId{0x53595354454D0000ULL, 0x000000000000F001ULL},
    .user_id = os::core::UserId{0U},
    .name = "system.echo",
    .restart_policy = os::supervisor::RestartPolicy::on_failure,
    .restart_delay_ms = 50U,
    .max_restarts_in_window = 3U,
    .restart_window_ms = 2000U,
    .readiness_timeout_ms = 1000U,
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 || std::strcmp(argv[1], "--echo-executable") != 0) {
        std::fprintf(stderr, "usage: os-supervisor --echo-executable PATH\n");
        return 2;
    }

    (void)::signal(SIGTERM, stop_handler);
    (void)::signal(SIGINT, stop_handler);

    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = echo_descriptor,
            .executable_path = argv[2],
        }
    };

    auto start_result = supervisor.start();
    if (!start_result) {
        std::fprintf(stderr, "os-supervisor: failed to start system.echo\n");
        return 3;
    }

    while (keep_running != 0) {
        auto maintain_result = supervisor.maintain();
        if (!maintain_result &&
            supervisor.status().state == os::supervisor::ServiceState::crash_loop) {
            std::fprintf(stderr, "os-supervisor: system.echo entered crash loop\n");
            return 4;
        }

        const timespec delay{.tv_sec = 0, .tv_nsec = 10'000'000L};
        (void)::nanosleep(&delay, nullptr);
    }
    return 0;
}
