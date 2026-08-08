#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/span.hpp>
#include <os/ipc/rpc.hpp>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = os::core::ByteSpan{
        reinterpret_cast<const std::byte*>(data), size};
    os::core::Error decoded{};
    (void)os::ipc::decode_rpc_error(bytes, decoded);
    return 0;
}
