#include <os/kernel/memory_grant.hpp>

#include <limits>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error grant_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] constexpr bool overlaps(
    std::uint64_t a_base, std::uint64_t a_length,
    std::uint64_t b_base, std::uint64_t b_length) noexcept {
    return a_base < b_base + b_length && b_base < a_base + a_length;
}

} // namespace

os::core::Result<MemoryGrant> MemoryGrantAuthority::create(
    std::uint64_t physical_base,
    std::uint64_t length) noexcept {
    // A range that wraps is refused rather than clamped. Every containment
    // check downstream compares base + length, and a wrapped range would
    // satisfy comparisons it has no business satisfying.
    if (length == 0ULL || length > UINT64_MAX - physical_base) {
        return os::core::Result<MemoryGrant>{grant_error(memory_grant_errors::invalid_range)};
    }

    for (const auto& slot : slots_) {
        if (!slot.occupied) continue;
        if (overlaps(slot.physical_base, slot.length, physical_base, length)) {
            return os::core::Result<MemoryGrant>{
                grant_error(memory_grant_errors::overlapping)};
        }
    }

    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (slot.occupied) continue;
        if (slot.generation == std::numeric_limits<MemoryGrantGeneration>::max()) {
            return os::core::Result<MemoryGrant>{
                grant_error(memory_grant_errors::generation_exhausted)};
        }
        ++slot.generation;
        // Generation zero is the never-issued value, so a slot that wrapped
        // into it would mint an identity that valid() rejects and nothing
        // could ever resolve. Refuse instead of issuing a dead name.
        if (slot.generation == 0U) {
            return os::core::Result<MemoryGrant>{
                grant_error(memory_grant_errors::generation_exhausted)};
        }
        slot.occupied = true;
        slot.physical_base = physical_base;
        slot.length = length;
        ++live_;
        return MemoryGrant{
            .identity = MemoryGrantIdentity{
                static_cast<MemoryGrantSlot>(index), slot.generation},
            .physical_base = physical_base,
            .length = length,
        };
    }
    return os::core::Result<MemoryGrant>{grant_error(memory_grant_errors::exhausted)};
}

os::core::Result<MemoryGrant> MemoryGrantAuthority::resolve(
    MemoryGrantIdentity identity) const noexcept {
    if (!identity.valid() || identity.slot >= slots_.size()) {
        return os::core::Result<MemoryGrant>{grant_error(memory_grant_errors::stale)};
    }
    const auto& slot = slots_[identity.slot];
    if (!slot.occupied || slot.generation != identity.generation) {
        return os::core::Result<MemoryGrant>{grant_error(memory_grant_errors::stale)};
    }
    return MemoryGrant{
        .identity = identity,
        .physical_base = slot.physical_base,
        .length = slot.length,
    };
}

os::core::Result<void> MemoryGrantAuthority::revoke(MemoryGrantIdentity identity) noexcept {
    if (!identity.valid() || identity.slot >= slots_.size()) {
        return grant_error(memory_grant_errors::stale);
    }
    auto& slot = slots_[identity.slot];
    if (!slot.occupied || slot.generation != identity.generation) {
        return grant_error(memory_grant_errors::stale);
    }
    // The generation is left where it is and bumped by the next create(), so
    // the slot's next occupant gets a name this identity does not match.
    slot.occupied = false;
    slot.physical_base = 0ULL;
    slot.length = 0ULL;
    --live_;
    return {};
}

} // namespace os::kernel
