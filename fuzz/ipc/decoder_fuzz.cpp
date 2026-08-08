#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/wire.hpp>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = os::core::ByteSpan{
        reinterpret_cast<const std::byte*>(data), size};

    os::ipc::Decoder decoder{bytes};
    auto header = os::ipc::decode_wire_header_v1(decoder);
    if (header) {
        const auto payload_size = static_cast<std::size_t>(header.value().payload_size);
        auto payload = decoder.read_raw(payload_size);
        if (payload) {
            (void)decoder.require_end();
        }
    }
    return 0;
}
