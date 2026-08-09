#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/rpc.hpp>
#include <os/supervisor/service_broker.hpp>

namespace {

constexpr os::core::ServiceId first_service_id{0x0000F091U};
constexpr os::core::ServiceId second_service_id{0x0000F092U};
constexpr os::core::PrincipalId first_service_principal{
    0x42524F4B45525331ULL,
    0x53595354454D3031ULL,
};
constexpr os::core::PrincipalId second_service_principal{
    0x42524F4B45525332ULL,
    0x53595354454D3032ULL,
};
constexpr os::core::PrincipalId client_principal{
    0x42524F4B45524150ULL,
    0x504C49434154494FULL,
};
constexpr os::core::PrincipalId wrong_principal{
    0x42524F4B45525752ULL,
    0x4F4E475052494E43ULL,
};
constexpr os::core::UserId client_user{290U};
constexpr std::uint32_t identify_operation = 1U;

os::supervisor::ServiceLaunchConfig make_config(
    os::core::ServiceId service,
    os::core::PrincipalId principal,
    const char* name,
    const char* executable) {
    return os::supervisor::ServiceLaunchConfig{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = service,
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

void expect_probe_identity(
    os::ipc::ClientConnection& connection,
    os::core::ServiceId service,
    os::core::ProcessId process,
    std::array<std::byte, os::ipc::max_wire_packet_size>& scratch) {
    auto response = connection.call(service, identify_operation, {}, scratch);
    assert(response);
    os::ipc::Decoder decoder{response.value().payload()};
    auto process_id = decoder.read_u64_le();
    auto principal_high = decoder.read_u64_le();
    auto principal_low = decoder.read_u64_le();
    auto user = decoder.read_u64_le();
    assert(process_id && principal_high && principal_low && user);
    assert(decoder.require_end());
    assert(process_id.value() == process.value());
    assert(principal_high.value() == client_principal.high);
    assert(principal_low.value() == client_principal.low);
    assert(user.value() == client_user.value());
}

void expect_unknown_sender(
    os::ipc::ClientConnection& connection,
    os::core::ServiceId service,
    std::array<std::byte, os::ipc::max_wire_packet_size>& scratch) {
    auto response = connection.call(service, identify_operation, {}, scratch);
    assert(!response);
    assert(response.error().domain == os::core::ErrorDomain::security);
    assert(response.error().code == os::core::errors::security::unknown_process);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 3);

    os::supervisor::ProcessAuthority authority;
    os::supervisor::Supervisor first{
        make_config(first_service_id, first_service_principal, "system.probe.first", argv[1]),
        authority,
    };
    os::supervisor::Supervisor second{
        make_config(second_service_id, second_service_principal, "system.probe.second", argv[2]),
        authority,
    };
    assert(first.start());
    assert(second.start());

    os::supervisor::ServiceBroker broker{authority};
    assert(broker.register_service(first_service_id, first));
    assert(broker.register_service(second_service_id, second));
    assert(broker.service_count() == 2U);

    // The directory is trusted and rejects an ID/supervisor mismatch rather
    // than becoming a caller-selected daemon-routing table.
    auto mismatched_registration = broker.register_service(
        os::core::ServiceId{0x0000F099U}, first);
    assert(!mismatched_registration);
    assert(mismatched_registration.error().domain == os::core::ErrorDomain::service);
    assert(mismatched_registration.error().code == os::supervisor::broker_errors::invalid_request);

    const std::array requested_services{first_service_id, second_service_id};
    auto attached = broker.attach_process(
        ::getpid(),
        client_principal,
        client_user,
        std::span<const os::core::ServiceId>{requested_services});
    assert(attached);
    const auto process = attached.value().peer.process;
    assert(process.value() != 0U);
    assert(broker.process_count() == 1U);

    // One base broker reference plus one publication per service still
    // describes one authoritative process record, not three logical identities.
    auto first_lookup = first.lookup_process(::getpid());
    auto second_lookup = second.lookup_process(::getpid());
    auto broker_lookup = broker.lookup(process);
    assert(first_lookup && second_lookup && broker_lookup);
    assert(first_lookup.value().peer == attached.value().peer);
    assert(second_lookup.value().peer == attached.value().peer);
    assert(broker_lookup.value().peer == attached.value().peer);

    auto first_channel_result = broker.connect(process, first_service_id);
    auto second_channel_result = broker.connect(process, second_service_id);
    assert(first_channel_result && second_channel_result);
    auto first_channel = std::move(first_channel_result).value();
    auto second_channel = std::move(second_channel_result).value();
    os::ipc::ClientConnection first_connection{first_channel};
    os::ipc::ClientConnection second_connection{second_channel};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    expect_probe_identity(first_connection, first_service_id, process, scratch);
    expect_probe_identity(second_connection, second_service_id, process, scratch);

    auto wrong_rebind = broker.attach_process(
        ::getpid(),
        wrong_principal,
        client_user,
        std::span<const os::core::ServiceId>{requested_services});
    assert(!wrong_rebind);
    assert(wrong_rebind.error().domain == os::core::ErrorDomain::security);
    assert(wrong_rebind.error().code == os::core::errors::security::credential_mismatch);

    const std::array duplicate_services{first_service_id, first_service_id};
    auto duplicate_request = broker.attach_process(
        ::getpid(),
        client_principal,
        client_user,
        std::span<const os::core::ServiceId>{duplicate_services});
    assert(!duplicate_request);
    assert(duplicate_request.error().domain == os::core::ErrorDomain::service);
    assert(duplicate_request.error().code == os::supervisor::broker_errors::invalid_request);

    const std::array unknown_services{os::core::ServiceId{0x0000F0FFU}};
    auto unknown_request = broker.attach_process(
        ::getpid(),
        client_principal,
        client_user,
        std::span<const os::core::ServiceId>{unknown_services});
    assert(!unknown_request);
    assert(unknown_request.error().domain == os::core::ErrorDomain::service);
    assert(unknown_request.error().code == os::supervisor::broker_errors::service_not_registered);

    assert(broker.detach_process(process));
    assert(broker.process_count() == 0U);
    expect_unknown_sender(first_connection, first_service_id, scratch);
    expect_unknown_sender(second_connection, second_service_id, scratch);

    auto gone = authority.lookup(process);
    assert(!gone);
    assert(gone.error().domain == os::core::ErrorDomain::security);
    assert(gone.error().code == os::core::errors::security::unknown_process);

    auto stale_connect = broker.connect(process, first_service_id);
    assert(!stale_connect);
    assert(stale_connect.error().domain == os::core::ErrorDomain::service);
    assert(stale_connect.error().code == os::supervisor::broker_errors::process_not_attached);

    // Explicit detach ends the old authorization epoch. A later trusted attach
    // of the still-live process receives a fresh boot-scoped ProcessId.
    auto reattached = broker.attach_process(
        ::getpid(),
        client_principal,
        client_user,
        std::span<const os::core::ServiceId>{requested_services});
    assert(reattached);
    assert(reattached.value().peer.process != process);
    assert(broker.detach_process(reattached.value().peer.process));
    return 0;
}
