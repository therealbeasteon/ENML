#include <cstddef>
#include <cstdint>
#include <cstring>

#include <os/kernel/ipc_syscall.hpp>

namespace {

[[nodiscard]] std::uint64_t read_u64(
    const std::uint8_t* data, std::size_t size, std::size_t offset) noexcept {
    std::uint64_t value = 0ULL;
    const std::size_t available = offset < size ? size - offset : 0U;
    const std::size_t take = available < sizeof(value) ? available : sizeof(value);
    if (take > 0U) std::memcpy(&value, data + offset, take);
    return value;
}

} // namespace

// os::kernel's own native IPC syscall decoders - decode_ipc_send_syscall and
// siblings - were the gap docs/ROADMAP.md named after M7.6a/M7.8 landed:
// fuzz/ipc/decoder_fuzz.cpp exercises os::ipc's service-layer RPC decoder,
// used over the Linux substrate's SOCK_SEQPACKET transport, not this. These
// are the bounded typed formats every other target in this tree is already
// fuzzed as - three raw register values in, no allocation, no user-memory
// access - so one iteration covers all three decoders directly from the
// input bytes rather than needing a script format the way the capability
// model (fuzz/kernel/capability_fuzz.cpp) does.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto x0 = read_u64(data, size, 0U);
    const auto x1 = read_u64(data, size, 8U);
    const auto x2 = read_u64(data, size, 16U);

    (void)os::kernel::decode_ipc_send_syscall(x0, x1, x2);
    (void)os::kernel::decode_ipc_receive_syscall(x0, x1, x2);
    (void)os::kernel::decode_ipc_reply_syscall(x0, x1, x2);
    return 0;
}
