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

// Three rights rather than one, because unlike an interrupt source there really
// are three different things to authorize. Holding a space so it can be mapped
// into is not the same authority as being able to destroy it: a pager may need
// the first over spaces it services and must not have the second.
//
// Admission is the third, and it is the one that must not be folded into
// `hold`. A pager holds every space it services; a pager that could also admit
// a thread could run code of its choosing inside every process it pages for,
// which is a strictly larger authority than paging and is held by a different
// principal. See docs/M7_12_ENTRY_BINDING.md.
inline constexpr Rights address_space_right_hold = 1U << 0U;
inline constexpr Rights address_space_right_destroy = 1U << 1U;

// The right to create address spaces at all, held over the one object below
// rather than over any particular space.
inline constexpr Rights address_space_right_create = 1U << 2U;

inline constexpr Rights address_space_right_admit = 1U << 3U;

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
// Both arguments are capabilities now, so there is one way to be malformed.
// The previous `invalid_page` was removed rather than left unused: an error
// code nothing returns is a code a reader has to look up to discover means
// nothing.
} // namespace address_space_syscall_errors

// KernelCall::address_space_create, two arguments per abi.cpp's descriptor.
//
// The second argument is a capability over memory, not a physical address.
// Creating a space needs a page and the kernel has no pool to take one from, so
// the caller supplies it - docs/M7_11_MEMORY.md's no-allocator decision - but
// what the caller names is its *authority* over a range rather than the range
// itself.
//
// That is a deliberate change from the first version, which took a page number.
// Two things were wrong with it. A process naming an arbitrary unclaimed page
// was appropriating memory nobody had given it, and nothing in the path said
// otherwise; and a syscall that takes a physical address teaches every caller
// the machine's physical layout, which is a disclosure with no purpose - a
// process has no business knowing where in RAM its page tables live.
struct AddressSpaceCreateSyscall final {
    CapabilityId authority {invalid_capability};
    CapabilityId root_grant {invalid_capability};
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
    std::uint64_t x1_root_grant) noexcept;

[[nodiscard]] os::core::Result<AddressSpaceDestroySyscall>
decode_address_space_destroy_syscall(std::uint64_t x0_space) noexcept;

} // namespace os::kernel
