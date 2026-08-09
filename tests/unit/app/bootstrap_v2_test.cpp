#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <os/app/bootstrap.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>

namespace {

constexpr os::core::ServiceId storage_service{0x0000F020U};
constexpr os::core::ServiceId key_service{0x0000F030U};

os::core::NativeHandle make_pipe_read_end(os::core::NativeHandle& write_end) {
    int descriptors[2]{-1, -1};
    assert(::pipe2(descriptors, O_CLOEXEC) == 0);
    write_end = os::core::NativeHandle{descriptors[1]};
    return os::core::NativeHandle{descriptors[0]};
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const os::app::ApplicationBootstrapRecordV1 record{
        .instance = os::core::ApplicationInstanceId{7U},
        .identity = os::core::PeerIdentity{
            .principal = os::core::PrincipalId{0x1111222233334444ULL, 0xAAAABBBBCCCCDDDDULL},
            .user = os::core::UserId{55U},
            .process = os::core::ProcessId{91U},
        },
        .package_generation = 3U,
    };

    os::core::NativeHandle first_write{};
    os::core::NativeHandle second_write{};
    auto first_read = make_pipe_read_end(first_write);
    auto second_read = make_pipe_read_end(second_write);
    const std::array services{storage_service, key_service};
    const std::array endpoints{
        os::core::NativeHandle{::fcntl(first_read.native(), F_DUPFD_CLOEXEC, 0)},
        os::core::NativeHandle{::fcntl(second_read.native(), F_DUPFD_CLOEXEC, 0)},
    };
    assert(endpoints[0].valid() && endpoints[1].valid());

    auto sent = os::app::send_bootstrap_request_v2(
        pair[0],
        record,
        std::span<const os::core::ServiceId>{services},
        std::span<const os::core::NativeHandle>{endpoints});
    assert(sent);

    std::array<std::byte, os::ipc::max_wire_packet_size> child_scratch{};
    auto received = os::app::receive_bootstrap_request_v2(pair[1], child_scratch);
    assert(received);
    auto request = std::move(received).value();
    assert(request.record == record);
    assert(request.service_count == services.size());
    assert(request.services[0] == storage_service);
    assert(request.services[1] == key_service);

    auto received_key = request.take_service_endpoint(key_service);
    auto received_storage = request.take_service_endpoint(storage_service);
    assert(received_key && received_storage);
    assert((::fcntl(received_key.value().native(), F_GETFD) & FD_CLOEXEC) != 0);
    assert((::fcntl(received_storage.value().native(), F_GETFD) & FD_CLOEXEC) != 0);

    // ServiceId, not array position or a serialized native fd number, selects
    // the transferred capability.
    const std::byte first_marker{0x31};
    const std::byte second_marker{0x52};
    assert(::write(first_write.native(), &first_marker, 1U) == 1);
    assert(::write(second_write.native(), &second_marker, 1U) == 1);
    std::byte observed{};
    assert(::read(received_storage.value().native(), &observed, 1U) == 1);
    assert(observed == first_marker);
    assert(::read(received_key.value().native(), &observed, 1U) == 1);
    assert(observed == second_marker);

    auto duplicate_take = request.take_service_endpoint(storage_service);
    assert(!duplicate_take);
    assert(duplicate_take.error().domain == os::core::ErrorDomain::ipc);
    assert(duplicate_take.error().code == os::ipc::errors::invalid_native_handle);

    auto ready = os::app::send_ready_v2(
        pair[1],
        request.request_header,
        request.record,
        std::span<const os::core::ServiceId>{services});
    assert(ready);
    std::array<std::byte, os::ipc::max_wire_packet_size> parent_scratch{};
    assert(os::app::wait_for_ready_v2(
        pair[0],
        parent_scratch,
        record,
        std::span<const os::core::ServiceId>{services},
        1000U));

    const std::array duplicate_services{storage_service, storage_service};
    auto duplicate_request = os::app::send_bootstrap_request_v2(
        pair[0],
        record,
        std::span<const os::core::ServiceId>{duplicate_services},
        std::span<const os::core::NativeHandle>{endpoints});
    assert(!duplicate_request);
    assert(duplicate_request.error().domain == os::core::ErrorDomain::service);
    assert(duplicate_request.error().code == os::core::errors::service::invalid_bootstrap);

    const std::array one_service{storage_service};
    auto count_mismatch = os::app::send_bootstrap_request_v2(
        pair[0],
        record,
        std::span<const os::core::ServiceId>{one_service},
        std::span<const os::core::NativeHandle>{endpoints});
    assert(!count_mismatch);
    assert(count_mismatch.error().domain == os::core::ErrorDomain::service);
    assert(count_mismatch.error().code == os::core::errors::service::invalid_bootstrap);
    return 0;
}
