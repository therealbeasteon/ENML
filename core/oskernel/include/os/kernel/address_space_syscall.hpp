#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/capability.hpp>

namespace os::kernel {

// Distinct tag so a capability over address space 3 can never be mistaken for
// one over interrupt source 3 or IPC endpoint 3, the same reasoning
// interrupt_object_tag and ipc_object_tag already document.
inline constexpr ObjectId address_space_object_tag = 0xADD5'0000'0000'0000ULL;
inline constexpr ObjectId address_space_object_tag_mask = 0xFFFF'0000'0000'0000ULL;

// The object id embeds the generation, not just the slot, and that is the whole
// security content of this encoding.
//
// M7.11's exit criteria require that a stale reference to a destroyed address
// space fails closed "with the same error a wrong reference gets, not a
// distinguishing one". Binding the generation into the identifier achieves that
// without any check written for the purpose: a capability minted over
// generation 4 of slot 2 simply does not name generation 5 of slot 2, so the
// ordinary capability lookup misses and returns the ordinary not-found error.
// A slot-only id would have needed a separate staleness comparison, which is a
// check that can be forgotten at one call site and a distinguishable answer
// that tells a caller a space it used to hold has been replaced.
[[nodiscard]] constexpr ObjectId address_space_object_id(
    AddressSpaceIdentity identity) noexcept {
    if (!identity.valid()) return invalid_object;
    return address_space_object_tag |
           (static_cast<ObjectId>(identity.generation) << 16U) |
           static_cast<ObjectId>(identity.slot);
}

// Two rights rather than one, because unlike an interrupt source there really
// are two different things to authorize. Holding a space so it can be mapped
// into is not the same authority as being able to destroy it: a pager may need
// the first over spaces it services and must not have the second.
inline constexpr Rights address_space_right_hold = 1U << 0U;
inline constexpr Rights address_space_right_destroy = 1U << 1U;

// The right to create address spaces at all, held over the one object below
// rather than over any particular space.
inline constexpr Rights address_space_right_create = 1U << 2U;

// The object that authorizes creation. It is the bare tag - slot 0,
// generation 0 - and that encoding is free by construction rather than by
// convention: address_space_object_id refuses generation 0, so no real space
// can ever be named by it. Reserving it here costs no namespace and cannot
// collide with a space that has not been created yet.
//
// This is transitional and should be said plainly. In the finished design the
// authority to create a space is the authority over the page it is built
// from - docs/M7_11_MEMORY.md's no-allocator decision makes memory the only
// authority there is - and the caller already supplies that page. Memory
// capabilities do not exist yet, so a distinguished object stands in for them.
// When they land, this object should disappear rather than being kept beside
// them: two answers to "who may create an address space" is one too many.
inline constexpr ObjectId address_space_authority_object = address_space_object_tag;

namespace address_space_syscall_errors {
inline constexpr std::uint32_t invalid_capability = 260U;
inline constexpr std::uint32_t invalid_page = 261U;
} // namespace address_space_syscall_errors

// KernelCall::address_space_create, two arguments per abi.cpp's descriptor.
//
// The root page is an argument because creating a space needs one and the
// kernel has no pool to take it from - the caller supplies it, as
// docs/M7_11_MEMORY.md's no-allocator decision requires. It is the same page
// the caller must already have donated; passing it here is what ties the
// created space to a specific page rather than to whatever happens to be at the
// head of an arena.
struct AddressSpaceCreateSyscall final {
    CapabilityId authority {invalid_capability};
    std::uint64_t root_page {0ULL};
};

// KernelCall::address_space_destroy, one argument. The capability names the
// space, and because the object id carries the generation, a capability over a
// space that has already been destroyed does not name its successor.
struct AddressSpaceDestroySyscall final {
    CapabilityId space {invalid_capability};
};

[[nodiscard]] os::core::Result<AddressSpaceCreateSyscall>
decode_address_space_create_syscall(
    std::uint64_t x0_authority,
    std::uint64_t x1_root_page) noexcept;

[[nodiscard]] os::core::Result<AddressSpaceDestroySyscall>
decode_address_space_destroy_syscall(std::uint64_t x0_space) noexcept;

} // namespace os::kernel
