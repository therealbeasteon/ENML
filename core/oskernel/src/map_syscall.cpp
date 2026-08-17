#include <os/kernel/map_syscall.hpp>

#include <os/core/error.hpp>
#include <os/kernel/machine.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error map_syscall_error(
    std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

// The third statement of read/read_write/read_execute in this repository, held
// to the other two by the compiler rather than by anyone remembering. The
// machine layer's is the one that has to be matched, because it is the one that
// ends up in a page table; `.ckx`'s is checked where `.ckx` is parsed.
static_assert(
    static_cast<std::uint8_t>(MapPermissions::read) ==
        static_cast<std::uint8_t>(MachinePermissions::read),
    "the ABI's permission values must be the machine layer's, or a caller asks "
    "for one thing and gets another");
static_assert(
    static_cast<std::uint8_t>(MapPermissions::read_write) ==
        static_cast<std::uint8_t>(MachinePermissions::read_write),
    "the ABI's permission values must be the machine layer's");
static_assert(
    static_cast<std::uint8_t>(MapPermissions::read_execute) ==
        static_cast<std::uint8_t>(MachinePermissions::read_execute),
    "the ABI's permission values must be the machine layer's");
} // namespace

os::core::Result<MapSyscall> decode_map_syscall(
    std::uint64_t x0_space,
    std::uint64_t x1_virtual_address,
    std::uint64_t x2_backing,
    std::uint64_t x3_permissions) noexcept {
    // Both capabilities, and zero is the only value this layer can reject about
    // either. Whether they name anything, and whether the caller holds them
    // with the rights this call needs, are capability-table questions answered
    // where the check is enforced.
    if (x0_space == 0ULL || x2_backing == 0ULL) {
        return map_syscall_error(map_syscall_errors::invalid_capability);
    }
    // Zero is refused, and it is the only address value refused here. It is
    // what a zeroed register set produces, so accepting it would let a caller
    // that supplied no address at all be told it had supplied one - the same
    // reason zero is never a valid call number and never a valid capability.
    if (x1_virtual_address == 0ULL) {
        return map_syscall_error(map_syscall_errors::invalid_address);
    }
    // An out-of-range value is refused rather than masked to its low bits. A
    // caller that asked for permissions this kernel cannot express should be
    // told so, not quietly given whichever of the three the truncation landed
    // on - and read_write is what a careless mask most often lands on.
    if (!valid_map_permissions(x3_permissions)) {
        return map_syscall_error(map_syscall_errors::invalid_permissions);
    }
    return MapSyscall{
        .space = static_cast<CapabilityId>(x0_space),
        .virtual_address = x1_virtual_address,
        .backing = static_cast<CapabilityId>(x2_backing),
        .permissions = static_cast<MapPermissions>(x3_permissions),
    };
}

} // namespace os::kernel
