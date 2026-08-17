#pragma once

#include <array>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>

// Which memory in an address space may execute, and therefore where a thread
// admitted into it begins.
//
// docs/M7_16_ENTRY_FROM_REGION.md is the decision and the reasoning. In one
// sentence: **Cookie has no entry-point argument anywhere, because the entry is
// derived from the space's executable region rather than named by anyone.**
//
// The shape is Apple's Secure Enclave Boot Monitor, which is handed "the address
// and size" of an image, makes that region executable, and "starts execution
// within the newly loaded code" - a request that carries a region and no entry,
// so there is no entry for a caller to get wrong. Cookie's `map` call with
// read_execute permission is the same request, and this table is what remembers
// the answer.
//
// The alternative designs all authorised an entry - deciding which principal may
// name one. This makes naming one unrepresentable, which is the move Cookie has
// already made for argv, for a program's knowledge of its own layout, for map's
// length, and for the syscall outcome tag. Each removed a capability rather than
// guarding it.
namespace os::kernel {

namespace executable_region_errors {
// The space already has an executable region. A second would make "where does
// this space begin" ambiguous, and the answer to an ambiguous question is the
// one an attacker picks.
inline constexpr std::uint32_t already_executable = 1U;
// Nothing executable has been established in this space, so there is nowhere for
// a thread to begin. Distinct from already_executable because the two are
// opposite mistakes and a caller can act on the difference.
inline constexpr std::uint32_t not_executable = 2U;
inline constexpr std::uint32_t invalid_space = 3U;
inline constexpr std::uint32_t exhausted = 4U;
} // namespace executable_region_errors

struct ExecutableRegion final {
    AddressSpaceIdentity space {};
    std::uint64_t base {0ULL};
    std::uint64_t length {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return space.valid() && length != 0ULL;
    }
};

// One entry per address space, fixed size, like every other table in this
// kernel. There is no allocation here for the reason docs/M7_11_MEMORY.md gives
// once for the whole design: a syscall that can fail for want of kernel memory
// is a denial-of-service channel, and a table that cannot grow cannot be grown
// by a caller.
class ExecutableRegionTable final {
public:
    // Records the region that makes a space executable, and refuses a second.
    //
    // Called *after* the machine layer has established the mapping, not before.
    // The order matters and is the opposite of what reads naturally: recording
    // first would leave a space claiming an executable region that the mapping
    // then failed to create, and a thread admitted into it would enter memory
    // that is not there. Refusing a second executable region is checked
    // separately by `would_refuse` before the mapping is attempted, so a caller
    // is told no before anything happens rather than after.
    [[nodiscard]] os::core::Result<void> record(
        AddressSpaceIdentity space,
        std::uint64_t base,
        std::uint64_t length) noexcept;

    // Whether recording would be refused, without recording. This is what the
    // authorization path consults, so that a second executable mapping is
    // refused before the machine layer does any work - and so that the
    // authorization stays const, which is what keeps it callable from a check
    // that must not have effects.
    [[nodiscard]] bool would_refuse(AddressSpaceIdentity space) const noexcept;

    // Where a thread admitted into this space begins. The base of the region,
    // and nothing else: not a caller's argument, not a stored entry, not a value
    // that travelled through userland.
    [[nodiscard]] os::core::Result<ExecutableRegion> region_for(
        AddressSpaceIdentity space) const noexcept;

    // Releases the slot when a space is destroyed. Without this a destroyed
    // space's identity would keep its slot and a later space reusing the slot
    // number would find it occupied - the same recycling hazard generations
    // exist to close, so the release is matched to the generation-bearing
    // identity rather than to the slot alone.
    [[nodiscard]] os::core::Result<void> forget(AddressSpaceIdentity space) noexcept;

    [[nodiscard]] std::size_t live_count() const noexcept { return live_; }

private:
    std::array<ExecutableRegion, max_address_space_epochs> regions_ {};
    std::size_t live_ {0U};
};

} // namespace os::kernel
