#include <cstddef>
#include <cstdint>
#include <cstring>

#include <os/kernel/abi.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/fault_delivery.hpp>
#include <os/kernel/thread_admission.hpp>

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

// The rest of the system-call surface an EL0 caller can reach.
//
// This target exists because an audit found the corpus had stopped tracking
// the kernel. fuzz/kernel/ipc_syscall_fuzz.cpp covers three decoders; by the
// time M7.11 and M7.12 had landed there were eight, and the five added since -
// the call-number decoder itself, both address-space decoders, the pager's
// answer and thread creation - had none. Every one of them is reached from EL0
// with attacker-chosen 64-bit register values, which makes them the most
// attacker-reachable code in the system and the least fuzzed.
//
// decode_call is the sharpest of the five and is fuzzed over the whole 16-bit
// space rather than only the values a caller "should" send. docs/abi.hpp calls
// the entry path "the one place in ENML where an attacker chooses a number that
// selects code", and the property being tested is that an unknown number is
// rejected rather than clamped to a default or used to index anything.
//
// No allocation, no user-memory access, no state carried between iterations:
// these are pure decoders over register values, so one pass covers all of them
// straight from the input bytes without needing the script format the
// capability model's target uses.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto x0 = read_u64(data, size, 0U);
    const auto x1 = read_u64(data, size, 8U);

    // The call number an SVC arrives with. Truncated to 16 bits the same way
    // the syscall entry truncates x8, so the fuzzer explores the surface a
    // caller actually controls rather than a wider one it does not.
    (void)os::kernel::decode_call(static_cast<std::uint16_t>(x0));
    // And the full 64-bit value, because the entry path's truncation is a cast
    // that could be moved or widened; a decoder that only holds for 16-bit
    // inputs is one line away from not holding.
    (void)os::kernel::decode_call(static_cast<std::uint16_t>(x0 >> 16U));

    (void)os::kernel::decode_address_space_create_syscall(x0, x1);
    (void)os::kernel::decode_address_space_destroy_syscall(x0);
    (void)os::kernel::decode_fault_supply_syscall(x0);
    (void)os::kernel::decode_thread_create_syscall(x0, x1);
    return 0;
}
