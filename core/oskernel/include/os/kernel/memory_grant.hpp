#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>

namespace os::kernel {

// Authority over physical memory, held by whoever was given it.
//
// docs/M7_11_MEMORY.md's central decision is that the kernel has no allocator:
// every kernel object is derived from memory a process already holds authority
// over. Until now that sentence had no referent - nothing recorded who held
// what, so `address_space_create` took a page number from a register and
// donated whatever it named. The reservation rules refused a page that was
// already reserved or user-mapped, which stops the worst cases, but a process
// naming an arbitrary unclaimed page was appropriating memory nobody gave it.
//
// This is the missing referent. A grant is a named range, and a capability over
// it is what a process presents to say "this is mine to spend".
//
// It is deliberately *not* an allocator and must not become one. There is no
// free list here and no operation that answers "give me a page" - the table
// only records ranges someone was handed and refuses to confirm authority
// nobody holds. A pool the kernel dispenses from would be a kernel heap by
// another name, and every argument in the no-allocator decision applies against
// it.

using MemoryGrantSlot = std::uint16_t;
using MemoryGrantGeneration = std::uint32_t;

inline constexpr std::size_t max_memory_grants = 64U;

// Identity, not index. The generation is part of the name for the same reason
// it is in AddressSpaceIdentity: a capability minted over a revoked grant must
// stop resolving on its own rather than by a staleness check some call site
// could forget, and must fail the way an unknown reference fails.
struct MemoryGrantIdentity final {
    MemoryGrantSlot slot {0U};
    MemoryGrantGeneration generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot < max_memory_grants && generation != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const MemoryGrantIdentity&, const MemoryGrantIdentity&) = default;
};

struct MemoryGrant final {
    MemoryGrantIdentity identity {};
    std::uint64_t physical_base {0ULL};
    std::uint64_t length {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return identity.valid() && length != 0ULL;
    }

    // Whether this grant covers a range entirely. Written to be safe at the
    // end of the address space rather than assuming no wrap: base + length can
    // overflow, and a caller naming a range that wraps must be refused rather
    // than accidentally satisfied by comparing wrapped values.
    [[nodiscard]] constexpr bool contains(
        std::uint64_t range_base,
        std::uint64_t range_length) const noexcept {
        if (!valid() || range_length == 0ULL) return false;
        if (range_base < physical_base) return false;
        if (range_length > UINT64_MAX - range_base) return false;
        if (length > UINT64_MAX - physical_base) return false;
        return range_base + range_length <= physical_base + length;
    }
};

namespace memory_grant_errors {
inline constexpr std::uint32_t exhausted = 1U;
inline constexpr std::uint32_t stale = 2U;
inline constexpr std::uint32_t invalid_range = 3U;
inline constexpr std::uint32_t generation_exhausted = 4U;
inline constexpr std::uint32_t overlapping = 5U;
} // namespace memory_grant_errors

// Distinct tag, so a capability over memory grant 3 can never be spent as one
// over address space 3, interrupt source 3 or IPC endpoint 3.
inline constexpr ObjectId memory_grant_object_tag = 0x0E1F'0000'0000'0000ULL;
inline constexpr ObjectId memory_grant_object_tag_mask = 0xFFFF'0000'0000'0000ULL;

[[nodiscard]] constexpr ObjectId memory_grant_object_id(
    MemoryGrantIdentity identity) noexcept {
    if (!identity.valid()) return invalid_object;
    return memory_grant_object_tag |
           (static_cast<ObjectId>(identity.generation) << 16U) |
           static_cast<ObjectId>(identity.slot);
}

// Two rights, because there are two different things a holder might be allowed
// to do with memory and they are not the same authority. Spending it on kernel
// objects - donating it to become a translation table - permanently changes
// what the range is, while mapping it is ordinary use. A process that should be
// able to use memory it was given but never turn it into kernel structures gets
// the second without the first.
inline constexpr Rights memory_right_map = 1U << 0U;
inline constexpr Rights memory_right_donate = 1U << 1U;

// The record of which physical ranges have been handed to someone.
//
// Ranges may not overlap. That is the invariant that makes a grant mean
// anything: two live grants over the same memory would let two holders each
// believe they may spend it, and the second to donate would be refused by the
// reservation rules for reasons that look like a bug rather than like a
// double-grant. Refusing at creation keeps the failure where the mistake is.
class MemoryGrantAuthority final {
public:
    [[nodiscard]] os::core::Result<MemoryGrant> create(
        std::uint64_t physical_base,
        std::uint64_t length) noexcept;

    // Refuses a revoked or unknown identity with `stale` - the same answer
    // either way, so a holder cannot learn that a grant it used to have has
    // been reissued to someone else.
    [[nodiscard]] os::core::Result<MemoryGrant> resolve(
        MemoryGrantIdentity identity) const noexcept;

    [[nodiscard]] os::core::Result<void> revoke(MemoryGrantIdentity identity) noexcept;

    [[nodiscard]] std::size_t live_count() const noexcept { return live_; }

private:
    struct Slot final {
        std::uint64_t physical_base {0ULL};
        std::uint64_t length {0ULL};
        MemoryGrantGeneration generation {0U};
        bool occupied {false};
    };

    std::array<Slot, max_memory_grants> slots_ {};
    std::size_t live_ {0U};
};

} // namespace os::kernel
