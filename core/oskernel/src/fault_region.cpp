#include <os/kernel/fault_region.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

constexpr std::uint64_t fault_region_granule = 4096ULL;

[[nodiscard]] constexpr os::core::Error region_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] constexpr bool aligned(std::uint64_t value) noexcept {
    return (value & (fault_region_granule - 1ULL)) == 0ULL;
}

[[nodiscard]] constexpr bool overlap(
    std::uint64_t a_base, std::uint64_t a_length,
    std::uint64_t b_base, std::uint64_t b_length) noexcept {
    return a_base < b_base + b_length && b_base < a_base + a_length;
}

} // namespace

os::core::Result<FaultRegionId> FaultRegionTable::declare(
    std::uint64_t base,
    std::uint64_t length,
    FaultDisclosure disclosure) noexcept {
    if (length == 0ULL || !aligned(base) || !aligned(length) ||
        base > UINT64_MAX - length) {
        return region_error(fault_region_errors::invalid_range);
    }
    for (const auto& slot : slots_) {
        if (slot.occupied && overlap(slot.base, slot.length, base, length)) {
            return region_error(fault_region_errors::overlaps);
        }
    }
    // Identifiers are never reused within an address space. A recycled id would
    // let a pager attribute a report to a region that has since been replaced,
    // which is the same stale-reference hazard the epoch authority exists to
    // prevent one level up.
    if (next_id_ == UINT16_MAX) {
        return region_error(fault_region_errors::exhausted);
    }
    Slot* free_slot = nullptr;
    for (auto& slot : slots_) {
        if (!slot.occupied) { free_slot = &slot; break; }
    }
    if (free_slot == nullptr) {
        return region_error(fault_region_errors::exhausted);
    }

    const FaultRegionId id = next_id_++;
    *free_slot = Slot{base, length, id, disclosure,
                      FaultRegionState::unbacked, false, false, true};
    ++occupied_;
    return id;
}

os::core::Result<void> FaultRegionTable::mark_backed(
    FaultRegionId region,
    FaultRegionState state) noexcept {
    auto* slot = find(region);
    if (slot == nullptr) return region_error(fault_region_errors::not_found);
    if (state == FaultRegionState::unbacked) {
        // Backing is not revocable through this interface, and that is the
        // point rather than an omission. If a pager could return a region to
        // the unbacked state it would hold the re-arm primitive the whole
        // design exists to withhold. Teardown destroys the region.
        return region_error(fault_region_errors::wrong_state);
    }
    slot->state = state;
    return {};
}

FaultReport FaultRegionTable::resolve(
    std::uint64_t virtual_address,
    bool write) noexcept {
    FaultReport report{};
    report.write = write;

    Slot* hit = nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && virtual_address >= slot.base &&
            virtual_address - slot.base < slot.length) {
            hit = &slot;
            break;
        }
    }

    // A fault outside every declared region is not a question anyone is
    // entitled to answer. Reporting it would tell a pager that an address it
    // has no responsibility for was touched, which is a channel with no
    // corresponding service.
    if (hit == nullptr) return report;

    if (hit->disclosure == FaultDisclosure::sealed) return report;

    // Each transition is announced at most once for the region's whole life.
    // A second fault of the same kind means backing did not appear, or that
    // something is probing; either way the answer is not to ask again.
    if (hit->state == FaultRegionState::unbacked) {
        if (hit->announced_backing) return report;
        hit->announced_backing = true;
    } else if (write && hit->state == FaultRegionState::backed_shared) {
        if (hit->announced_private) return report;
        hit->announced_private = true;
    } else {
        // Backed, and the access is one the backing already permits. This is a
        // permission violation, not a demand for memory.
        return report;
    }

    report.region = hit->id;
    report.disposition = FaultDisposition::deliver;
    return report;
}

os::core::Result<FaultRegionState> FaultRegionTable::state(
    FaultRegionId region) const noexcept {
    const auto* slot = find(region);
    if (slot == nullptr) return region_error(fault_region_errors::not_found);
    return slot->state;
}

const FaultRegionTable::Slot* FaultRegionTable::find(
    FaultRegionId region) const noexcept {
    if (region == invalid_fault_region) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.id == region) return &slot;
    }
    return nullptr;
}

FaultRegionTable::Slot* FaultRegionTable::find(FaultRegionId region) noexcept {
    return const_cast<Slot*>(
        static_cast<const FaultRegionTable*>(this)->find(region));
}

} // namespace os::kernel
