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
    std::uint64_t x1_root_page) noexcept {
    if (x0_authority == 0ULL) {
        return address_space_syscall_error(
            address_space_syscall_errors::invalid_capability);
    }
    // Zero is the only value this layer can reject. Page alignment and whether
    // the caller actually owns this page are deliberately not checked here:
    // alignment is an architectural property the portable decoder would have to
    // import a machine header to know, and ownership is a ledger question that
    // aarch64_donate_table_page already answers by refusing a page some process
    // can still reach. Re-checking either here would put a second, weaker copy
    // of a rule beside the enforcing one, which is how the two come to disagree.
    if (x1_root_page == 0ULL) {
        return address_space_syscall_error(address_space_syscall_errors::invalid_page);
    }
    return AddressSpaceCreateSyscall{
        .authority = static_cast<CapabilityId>(x0_authority),
        .root_page = x1_root_page,
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
