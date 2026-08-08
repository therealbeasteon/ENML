#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/supervisor/supervisor.hpp>

#include "echo_client.generated.hpp"

namespace generated = os::test::echo;

namespace {

constexpr os::core::PrincipalId service_principal{
    0x53595354454D0000ULL,
    0x000000000000F001ULL,
};
constexpr os::core::PrincipalId client_principal{
    0x5445535400000000ULL,
    0x0000000000000042ULL,
};
constexpr os::core::UserId client_user{42U};

void sleep_ms(long milliseconds) {
    const timespec delay{
        .tv_sec = milliseconds / 1000L,
        .tv_nsec = (milliseconds % 1000L) * 1'000'000L,
    };
    (void)::nanosleep(&delay, nullptr);
}

bool wait_for_running(os::supervisor::Supervisor& supervisor, os::core::ProcessId previous) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        (void)supervisor.maintain();
        const auto status = supervisor.status();
        if (status.state == os::supervisor::ServiceState::running &&
            status.identity.process.value() != 0U && status.identity.process != previous) {
            return true;
        }
        sleep_ms(5L);
    }
    return false;
}

void expect_security_error(
    os::core::Result<generated::PingResponse>& result,
    std::uint32_t code) {
    assert(!result);
    assert(result.error().domain == os::core::ErrorDomain::security);
    assert(result.error().code == code);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    const os::supervisor::ServiceDescriptorV1 descriptor{
        .service_id = generated::EchoClient::service_id,
        .principal_id = service_principal,
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
    assert(supervisor.start());

    auto unregistered_channel_result = supervisor.connect();
    assert(unregistered_channel_result);
    auto unregistered_channel = std::move(unregistered_channel_result).value();
    os::ipc::ClientConnection unregistered_connection{unregistered_channel};
    generated::EchoClient unregistered_client{unregistered_connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> response_buffer{};
    auto unregistered_ping = unregistered_client.ping(
        generated::PingRequest{.text = "not-registered"}, response_buffer);
    expect_security_error(unregistered_ping, os::core::errors::security::unknown_process);

    auto registration_result = supervisor.register_process(::getpid(), client_principal, client_user);
    assert(registration_result);
    const auto registration = registration_result.value();
    assert(registration.peer.process.value() != 0U);
    assert(registration.peer.principal == client_principal);
    assert(registration.peer.user == client_user);
    assert(registration.kernel.process_id == static_cast<std::int64_t>(::getpid()));

    auto lookup_result = supervisor.lookup_process(::getpid());
    assert(lookup_result);
    assert(lookup_result.value().peer == registration.peer);

    auto channel_result = supervisor.connect();
    assert(channel_result);
    auto channel = std::move(channel_result).value();
    os::ipc::ClientConnection connection{channel};
    generated::EchoClient client{connection};

    auto identify_result = client.identify(generated::Empty{}, response_buffer);
    assert(identify_result);
    assert(identify_result.value().process_id == registration.peer.process.value());
    assert(identify_result.value().principal_high == client_principal.high);
    assert(identify_result.value().principal_low == client_principal.low);
    assert(identify_result.value().user_id == client_user.value());

    auto forged_result = client.identify_claim(
        generated::IdentityClaimRequest{
            .claimed_process_id = 0xFFFFFFFFFFFFFFFFULL,
            .claimed_principal_high = 0x53595354454D0000ULL,
            .claimed_principal_low = 0x0000000000009999ULL,
            .claimed_user_id = 0xFFFFFFFFFFFFFFFFULL,
        }, response_buffer);
    assert(forged_result);
    assert(forged_result.value().process_id == registration.peer.process.value());
    assert(forged_result.value().principal_high == client_principal.high);
    assert(forged_result.value().principal_low == client_principal.low);
    assert(forged_result.value().user_id == client_user.value());

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        auto child_ping = client.ping(generated::PingRequest{.text = "stolen-channel"}, response_buffer);
        if (!child_ping && child_ping.error().domain == os::core::ErrorDomain::security &&
            child_ping.error().code == os::core::errors::security::unknown_process) {
            std::_Exit(0);
        }
        std::_Exit(20);
    }
    int child_status = 0;
    assert(::waitpid(child, &child_status, 0) == child);
    assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);

    auto parent_ping = client.ping(generated::PingRequest{.text = "still-parent"}, response_buffer);
    assert(parent_ping);
    assert(parent_ping.value().text == "still-parent");

    const auto first_service_process = supervisor.status().identity.process;
    assert(supervisor.terminate(SIGKILL));
    sleep_ms(10L);
    assert(wait_for_running(supervisor, first_service_process));
    assert(supervisor.status().identity.process != first_service_process);

    auto restarted_channel_result = supervisor.connect();
    assert(restarted_channel_result);
    auto restarted_channel = std::move(restarted_channel_result).value();
    os::ipc::ClientConnection restarted_connection{restarted_channel};
    generated::EchoClient restarted_client{restarted_connection};
    auto restarted_identity = restarted_client.identify(generated::Empty{}, response_buffer);
    assert(restarted_identity);
    assert(restarted_identity.value().process_id == registration.peer.process.value());
    assert(restarted_identity.value().principal_high == client_principal.high);
    assert(restarted_identity.value().principal_low == client_principal.low);

    assert(supervisor.unregister_process(registration.peer.process));
    auto revoked_ping = restarted_client.ping(
        generated::PingRequest{.text = "revoked"}, response_buffer);
    expect_security_error(revoked_ping, os::core::errors::security::unknown_process);

    auto revoked_lookup = supervisor.lookup_process(::getpid());
    assert(!revoked_lookup);
    assert(revoked_lookup.error().domain == os::core::ErrorDomain::security);
    assert(revoked_lookup.error().code == os::core::errors::security::unknown_process);
    return 0;
}
