#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>

#include <os/core/strong_id.hpp>
#include <os/ipc/encoder.hpp>
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
    using namespace os;

    const ipc::WireHeaderV1 header {
        .flags = ipc::flag_value(ipc::WireFlag::request),
        .service_id = core::ServiceId{0x0000F001U},
        .operation_id = 1U,
        .request_id = core::RequestId{1U},
        .payload_size = 0U,
        .handle_count = 0U,
        .payload_checksum = 0U,
    };

    std::array<std::byte, ipc::wire_header_v1_size> storage {};
    ipc::Encoder encoder{storage};

    const auto before = allocation_count;
    const auto result = ipc::encode_wire_header_v1(encoder, header);
    const auto after = allocation_count;

    assert(result);
    assert(after == before);
    return 0;
}
