#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/capability.hpp>

// The `map` call: establishing a mapping in an address space.
//
// docs/M7_16_MAP.md is the reasoning and makes the two decisions worth not
// relitigating - that the required right on the space is
// `address_space_right_hold` rather than a new one, and that **the length comes
// from the grant rather than from the caller**, because a length argument is a
// second statement of something the authority already says and the two can
// disagree.
//
// It was also the third occurrence of a defect class this project keeps
// finding: `map` has been in the sixteen-entry ABI table, with an authority
// class and an argument count and a comment about why it is authority rather
// than convenience, since before the kernel existed - and had no decoder, no
// kernel operation, no dispatch and no stub. Nothing consumed the declaration
// until something tried to write a program against it.
namespace os::kernel {

namespace map_syscall_errors {
// Zero names nothing. A zeroed register set must not describe a valid mapping,
// which is the same reason zero is never a valid call number.
inline constexpr std::uint32_t invalid_capability = 1U;
inline constexpr std::uint32_t invalid_address = 2U;
inline constexpr std::uint32_t invalid_permissions = 3U;
// The caller holds the space and the backing, and the backing's extent is not
// mappable at the address asked for - the sum would leave the address space.
// Distinct from invalid_address because the address alone was fine.
inline constexpr std::uint32_t range_overflows = 4U;
} // namespace map_syscall_errors

// The same three the machine layer has, and the same three `.ckx` mirrors.
//
// A third statement of one set of values, so the agreement is a `static_assert`
// in map_syscall.cpp rather than an intention - the fix M7.15a applied to the
// outcome tags after defining them in two places was caught one commit later.
// A fourth value here would be a permission the kernel cannot express, which is
// a promise this call could not keep.
//
// Not simply `MachinePermissions` itself, because this is an ABI surface: a
// program compiled today against `read_execute = 3` must keep meaning that, and
// tying the number a caller writes into a register to a machine-layer enum
// would let a machine-layer renumbering silently change what compiled programs
// ask for. `.ckx` declined the same reuse for the same reason.
enum class MapPermissions : std::uint8_t {
    read = 1U,
    read_write = 2U,
    read_execute = 3U,
};

[[nodiscard]] constexpr bool valid_map_permissions(std::uint64_t value) noexcept {
    return value == static_cast<std::uint64_t>(MapPermissions::read) ||
           value == static_cast<std::uint64_t>(MapPermissions::read_write) ||
           value == static_cast<std::uint64_t>(MapPermissions::read_execute);
}

struct MapSyscall final {
    // Which space, as a capability. Never an identifier: an identifier is a
    // number a caller can guess, and the refusal it earns says which spaces
    // exist.
    CapabilityId space {invalid_capability};
    std::uint64_t virtual_address {0ULL};
    // What to map, as a capability over a MemoryGrant. Never a physical
    // address - a caller naming physical memory directly would be asserting an
    // authority the physical ledger exists to check.
    CapabilityId backing {invalid_capability};
    MapPermissions permissions {MapPermissions::read};
};

// Decodes the four registers, and validates only what is wrong about the
// *encoding*.
//
// Alignment and address range are deliberately not checked here. They are
// AArch64 questions about an AArch64 address layout, the machine layer already
// enforces both (`aarch64_map_user` against `Stage1Region::lower`), and a
// second weaker copy of a rule beside the one that enforces it is how the two
// come to disagree - which is what the capability decoders in
// address_space_syscall.cpp already say about capabilities. Checking the page
// granule here would additionally make this a third statement of 4096.
[[nodiscard]] os::core::Result<MapSyscall> decode_map_syscall(
    std::uint64_t x0_space,
    std::uint64_t x1_virtual_address,
    std::uint64_t x2_backing,
    std::uint64_t x3_permissions) noexcept;

// What the kernel resolved, and what the machine layer is to perform.
//
// It carries the resolved physical base rather than the capability that named
// it, deliberately: the machine layer must not re-resolve, because a second
// resolution is a second answer and the window between them is where the
// capability could have been revoked.
struct MapAuthorization final {
    AddressSpaceIdentity space {};
    std::uint64_t virtual_base {0ULL};
    std::uint64_t physical_base {0ULL};
    // From the grant, never from the caller. See docs/M7_16_MAP.md.
    std::uint64_t length {0ULL};
    MapPermissions permissions {MapPermissions::read};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return space.valid() && virtual_base != 0ULL && length != 0ULL;
    }
};

} // namespace os::kernel
