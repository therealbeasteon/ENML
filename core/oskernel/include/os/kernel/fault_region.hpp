#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

namespace os::kernel {

namespace fault_region_errors {
inline constexpr std::uint32_t invalid_range = 1U;
inline constexpr std::uint32_t overlaps = 2U;
inline constexpr std::uint32_t exhausted = 3U;
inline constexpr std::uint32_t not_found = 4U;
inline constexpr std::uint32_t wrong_state = 5U;
} // namespace fault_region_errors

using FaultRegionId = std::uint16_t;
inline constexpr FaultRegionId invalid_fault_region = 0U;
inline constexpr std::size_t max_fault_regions = 32U;

// What userland is permitted to learn about faults in a region.
enum class FaultDisclosure : std::uint8_t {
    // A pager may be asked to back this region. It is told the region, never
    // the address, and only on a lifecycle transition - see FaultRegionTable.
    paged = 1U,
    // No pager is ever asked. The region must be backed before it is reachable,
    // and a fault in it terminates the faulting thread. This is the memory a
    // process puts key material, plaintext and any secret-dependent index in:
    // it cannot produce a fault a userland process observes, so it cannot carry
    // an access pattern out of the address space.
    sealed = 2U,
};

enum class FaultRegionState : std::uint8_t {
    unbacked = 0U,
    backed_shared = 1U,
    backed_private = 2U,
};

enum class FaultDisposition : std::uint8_t {
    // Ask the region's pager for backing. Carries a region, not an address.
    deliver = 1U,
    // Nobody may be asked. The faulting thread dies; the machine does not.
    terminate = 2U,
};

// What the kernel is willing to say about a fault.
//
// There is deliberately no faulting address in this structure, and adding one
// would undo the whole point - see docs/M7_11_FAULT_PRIVACY.md. `region` is an
// identifier the faulting process chose the extent of when it declared the
// region, so the resolution of anything a pager can infer is the process's own
// decision rather than the hardware's page size.
struct FaultReport final {
    FaultRegionId region {invalid_fault_region};
    FaultDisposition disposition {FaultDisposition::terminate};
    bool write {false};

    [[nodiscard]] constexpr bool deliverable() const noexcept {
        return disposition == FaultDisposition::deliver &&
               region != invalid_fault_region;
    }
};

// Per-address-space record of which virtual ranges a fault may be asked about.
//
// The controlled-channel attack (Xu/Cui/Peinado 2015, and Van Bulck's stealthier
// page-table variants) needs two things from its victim's kernel: the address
// that faulted, and the ability to re-arm by revoking access again. A microkernel
// that exports paging to userland - which Cookie does deliberately, because
// layout policy does not belong in a kernel - hands a pager both by default.
// L4 and seL4 synthesise a fault message carrying the faulting address, and the
// pager owns the frames and therefore the mappings, so it can unmap and ask
// again as often as it likes.
//
// Cookie gives it neither, and can do it in software because the kernel is
// trusted here: the enclave literature needs an ISA change only because in that
// setting the kernel is the adversary.
//
//  * No address. resolve() reports the declared region containing the fault.
//    Granularity is whatever the process asked for, not 4 KiB.
//  * No re-arm. A region is deliverable only on a state transition it has not
//    already made, and each transition happens once. The pager supplies
//    backing; it never holds unmap authority over a live region, so it cannot
//    return the region to a faulting state to be told again.
//
// The residual channel is stated rather than papered over: first touch of each
// region, and the order regions are first touched in, remain observable. That
// is bounded by the number of regions and their transitions - at most two
// events per region for its whole lifetime - instead of an unbounded trace at
// page resolution. `sealed` exists for the memory where even that is too much.
class FaultRegionTable final {
public:
    // Declares a virtual range whose faults have a defined answer. Ranges may
    // not overlap: an address resolving to two regions would make the report
    // depend on search order, which is exactly the kind of ambiguity a
    // disclosure decision must not rest on.
    [[nodiscard]] os::core::Result<FaultRegionId> declare(
        std::uint64_t base,
        std::uint64_t length,
        FaultDisclosure disclosure) noexcept;

    // Records that backing now exists. Called by the kernel after it installs
    // the mapping, never by the pager: the pager supplies frames and does not
    // decide when a region stops being able to fault.
    [[nodiscard]] os::core::Result<void> mark_backed(
        FaultRegionId region,
        FaultRegionState state) noexcept;

    // The only thing the fault path is allowed to consult, and the only thing
    // that produces a FaultReport.
    //
    // Deliberately not const: resolving *consumes* the region's one announcement
    // of that transition. It has to, and the alternative was tried first and was
    // wrong - a query that leaves the one-shot unspent lets the same transition
    // be reported repeatedly, which is precisely the re-arm primitive this class
    // exists to withhold. The announcement is spent even if delivery downstream
    // fails, because "the pager did not answer" must not be a way to be asked
    // again.
    [[nodiscard]] FaultReport resolve(
        std::uint64_t virtual_address,
        bool write) noexcept;

    [[nodiscard]] os::core::Result<FaultRegionState> state(
        FaultRegionId region) const noexcept;

    [[nodiscard]] std::size_t count() const noexcept { return occupied_; }

private:
    struct Slot final {
        std::uint64_t base {0ULL};
        std::uint64_t length {0ULL};
        FaultRegionId id {invalid_fault_region};
        FaultDisclosure disclosure {FaultDisclosure::sealed};
        FaultRegionState state {FaultRegionState::unbacked};
        // One bit per transition that has already been announced. A region that
        // has been reported unbacked once is never reported unbacked again,
        // even if something later returns it to that state.
        bool announced_backing {false};
        bool announced_private {false};
        bool occupied {false};
    };

    std::array<Slot, max_fault_regions> slots_ {};
    std::size_t occupied_ {0U};
    FaultRegionId next_id_ {1U};

    [[nodiscard]] const Slot* find(FaultRegionId region) const noexcept;
    [[nodiscard]] Slot* find(FaultRegionId region) noexcept;
};

} // namespace os::kernel
