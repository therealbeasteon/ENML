#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>

#include <unistd.h>

#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/wire.hpp>

namespace {
std::size_t allocation_count = 0;
}

void* operator new(std::size_t size) {
    ++allocation_count;
    if (void* memory = std::malloc(size)) {
        return memory;
    }
    std::abort();
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const os::ipc::WireHeaderV1 header {
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = os::core::ServiceId{0x0000F001U},
        .operation_id = 1U,
        .request_id = os::core::RequestId{1U},
        .payload_size = 0U,
        .handle_count = 0U,
        .payload_checksum = 0U,
    };

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer {};

    const auto before_send = allocation_count;
    auto send_result = pair[0].send(header, {});
    const auto after_send = allocation_count;
    assert(send_result);
    assert(after_send == before_send);

    const auto before_receive = allocation_count;
    auto receive_result = pair[1].receive(receive_buffer);
    const auto after_receive = allocation_count;
    assert(receive_result);
    assert(after_receive == before_receive);

    auto message = std::move(receive_result).value();
    assert(message.sender_credentials().process_id == static_cast<std::int64_t>(::getpid()));
    return 0;
}
