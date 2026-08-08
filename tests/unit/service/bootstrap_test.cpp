#include <array>
#include <cassert>
#include <cstddef>

#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const os::service::BootstrapRecordV1 record{
        .identity = os::core::PeerIdentity{
            .principal = os::core::PrincipalId{0x1111U, 0x2222U},
            .user = os::core::UserId{0U},
            .process = os::core::ProcessId{42U},
        },
        .service_id = os::core::ServiceId{0xF001U},
        .boot_generation = 7U,
    };
    assert(os::service::send_bootstrap_request(pair[0], record));

    std::array<std::byte, os::ipc::max_wire_packet_size> child_buffer{};
    auto receive_result = os::service::receive_bootstrap_request(
        pair[1], child_buffer, record.service_id);
    assert(receive_result);
    const auto request = receive_result.value();
    assert(request.record.identity == record.identity);
    assert(request.record.service_id == record.service_id);
    assert(request.record.boot_generation == record.boot_generation);

    assert(os::service::send_ready(pair[1], request.request_header));
    std::array<std::byte, os::ipc::max_wire_packet_size> parent_buffer{};
    assert(os::service::wait_for_ready(pair[0], parent_buffer, 100U));

    auto bad_pair_result = os::ipc::Channel::create_local_pair();
    assert(bad_pair_result);
    auto bad_pair = std::move(bad_pair_result).value();
    assert(os::service::send_bootstrap_request(bad_pair[0], record));
    auto bad_receive = os::service::receive_bootstrap_request(
        bad_pair[1], child_buffer, os::core::ServiceId{0xBEEFU});
    assert(!bad_receive);
    assert(bad_receive.error().domain == os::core::ErrorDomain::service);
    assert(bad_receive.error().code == os::core::errors::service::invalid_bootstrap);
    return 0;
}
