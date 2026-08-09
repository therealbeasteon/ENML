#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/supervisor/process_authority.hpp>
#include <os/supervisor/supervisor.hpp>

#include "echo_client.generated.hpp"

namespace generated = os::test::echo;

namespace {

constexpr os::core::PrincipalId first_service_principal{
    0x53595354454D0000ULL,
    0x000000000000F101ULL,
};
constexpr os::core::PrincipalId second_service_principal{
    0x53595354454D0000ULL,
    0x000000000000F102ULL,
};
constexpr os::core::PrincipalId client_principal{
    0x4D323950524F4331ULL,
    0x4944454E54495459ULL,
};
constexpr os::core::PrincipalId wrong_principal{
    0x4D323957524F4E47ULL,
    0x4944454E54495459ULL,
};
constexpr os::core::UserId client_user{209U};

os::supervisor::ServiceLaunchConfig make_config(
    const char* executable,
    os::core::PrincipalId principal,
    const char* name) {
    return os::supervisor::ServiceLaunchConfig{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = generated::EchoClient::service_id,
            .principal_id = principal,
            .user_id = os::core::UserId{0U},
            .name = name,
            .restart_policy = os::supervisor::RestartPolicy::on_failure,
            .restart_delay_ms = 10U,
            .max_restarts_in_window = 3U,
            .restart_window_ms = 2000U,
            .readiness_timeout_ms = 1000U,
        },
        .executable_path = executable,
    };
}

void expect_unknown_process(os::core::Result<generated::IdentityResponse>& result) {
    assert(!result);
    assert(result.error().domain == os::core::ErrorDomain::security);
    assert(result.error().code == os::core::errors::security::unknown_process);
}

void expect_identity(
    generated::EchoClient& client,
    os::core::ProcessId process,
    std::array<std::byte, os::ipc::max_wire_packet_size>& scratch) {
    auto identity = client.identify(generated::Empty{}, scratch);
    assert(identity);
    assert(identity.value().process_id == process.value());
    assert(identity.value().principal_high == client_principal.high);
    assert(identity.value().principal_low == client_principal.low);
    assert(identity.value().user_id == client_user.value());
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    os::supervisor::ProcessAuthority authority;
    os::supervisor::Supervisor first{
        make_config(argv[1], first_service_principal, "system.echo.first"),
        authority,
    };
    os::supervisor::Supervisor second{
        make_config(argv[1], second_service_principal, "system.echo.second"),
        authority,
    };

    assert(first.start());
    assert(second.start());
    assert(first.status().identity.process.value() != 0U);
    assert(second.status().identity.process.value() != 0U);
    assert(first.status().identity.process != second.status().identity.process);
    assert(authority.size() == 2U);

    auto first_registration = first.register_process(::getpid(), client_principal, client_user);
    assert(first_registration);
    const auto shared_process = first_registration.value().peer.process;
    assert(shared_process.value() != 0U);
    assert(authority.size() == 3U);

    auto second_registration = second.register_process(::getpid(), client_principal, client_user);
    assert(second_registration);
    assert(second_registration.value().peer == first_registration.value().peer);
    assert(second_registration.value().peer.process == shared_process);
    assert(authority.size() == 3U);

    auto first_channel_result = first.connect();
    auto second_channel_result = second.connect();
    assert(first_channel_result && second_channel_result);
    auto first_channel = std::move(first_channel_result).value();
    auto second_channel = std::move(second_channel_result).value();
    os::ipc::ClientConnection first_connection{first_channel};
    os::ipc::ClientConnection second_connection{second_channel};
    generated::EchoClient first_client{first_connection};
    generated::EchoClient second_client{second_connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    // The same native sender resolves to exactly the same ENML identity in two
    // independently supervised service processes.
    expect_identity(first_client, shared_process, scratch);
    expect_identity(second_client, shared_process, scratch);

    // Revoking one service's publication must not destroy the boot-scoped
    // identity while another service still references it.
    assert(first.unregister_process(shared_process));
    auto first_revoked = first_client.identify(generated::Empty{}, scratch);
    expect_unknown_process(first_revoked);
    expect_identity(second_client, shared_process, scratch);
    auto authoritative_after_first_release = authority.lookup(shared_process);
    assert(authoritative_after_first_release);
    assert(authority.size() == 3U);

    // Final publication release removes the authoritative execution identity.
    assert(second.unregister_process(shared_process));
    auto second_revoked = second_client.identify(generated::Empty{}, scratch);
    expect_unknown_process(second_revoked);
    auto gone = authority.lookup(shared_process);
    assert(!gone);
    assert(gone.error().domain == os::core::ErrorDomain::security);
    assert(gone.error().code == os::core::errors::security::unknown_process);
    assert(authority.size() == 2U);

    // Explicit revocation creates a new authorization epoch. The same still-
    // live Linux process receives a fresh ProcessId rather than recovering the
    // revoked logical identity.
    auto fresh_registration = first.register_process(::getpid(), client_principal, client_user);
    assert(fresh_registration);
    const auto fresh_process = fresh_registration.value().peer.process;
    assert(fresh_process.value() != 0U);
    assert(fresh_process != shared_process);
    expect_identity(first_client, fresh_process, scratch);

    // A second service cannot rebind that live native process to a different
    // durable principal merely because it asks the shared authority.
    auto wrong_registration = second.register_process(::getpid(), wrong_principal, client_user);
    assert(!wrong_registration);
    assert(wrong_registration.error().domain == os::core::ErrorDomain::security);
    assert(wrong_registration.error().code == os::core::errors::security::credential_mismatch);

    assert(first.unregister_process(fresh_process));
    return 0;
}
