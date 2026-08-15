#include <os/kernel/address_space_syscall.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error address_space_syscall_error(
    std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
} // namespace

os::core::Result<AddressSpaceCreateSyscall> decode_address_space_create_syscall(
    std::uint64_t x0_authority,
    std::uint64_t x1_root_grant) noexcept {
    // Both are capabilities, and zero is the only value this layer can reject.
    // Whether either names anything, and whether the holder holds it, are
    // capability-table questions answered where the check is enforced. A second
    // weaker copy of a rule beside the enforcing one is how the two come to
    // disagree.
    if (x0_authority == 0ULL || x1_root_grant == 0ULL) {
        return address_space_syscall_error(
            address_space_syscall_errors::invalid_capability);
    }
    return AddressSpaceCreateSyscall{
        .authority = static_cast<CapabilityId>(x0_authority),
        .root_grant = static_cast<CapabilityId>(x1_root_grant),
    };
}

os::core::Result<AddressSpaceDestroySyscall> decode_address_space_destroy_syscall(
    std::uint64_t x0_space) noexcept {
    if (x0_space == 0ULL) {
        return address_space_syscall_error(
            address_space_syscall_errors::invalid_capability);
    }
    return AddressSpaceDestroySyscall{
        .space = static_cast<CapabilityId>(x0_space),
    };
}

} // namespace os::kernel
