#include <os/time/protection.hpp>

#include <os/core/error.hpp>

namespace os::time {
namespace {

[[nodiscard]] constexpr os::core::Error time_error(std::uint32_t code) noexcept {
    // Partitioning decides whether one principal can observe another's timing,
    // so a refusal here is a security answer rather than a service fault.
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

} // namespace

os::core::Result<PartitionLedger> PartitionLedger::create(
    std::uint32_t capabilities,
    std::uint32_t capacity,
    std::uint32_t shared_floor) noexcept {
    using ResultType = os::core::Result<PartitionLedger>;

    // Unknown capability bits are rejected like every other discriminant in the
    // tree. A bit nobody handles is a code path an attacker chooses.
    if ((capabilities & ~known_time_protection_capabilities) != 0U) {
        return ResultType{time_error(errors::unsupported_platform)};
    }

    // A ledger with capacity and no mechanism to back it would hand out
    // reservations that partition nothing, which is worse than refusing: the
    // holder would believe it was isolated.
    if (capacity != 0U && capabilities == 0U) {
        return ResultType{time_error(errors::unsupported_platform)};
    }

    // Something must always be left for everyone who did not ask. A floor of
    // zero would let one principal take the whole resource and evict the rest
    // of the system permanently.
    if (capacity != 0U && (shared_floor == 0U || shared_floor >= capacity)) {
        return ResultType{time_error(errors::invalid_request)};
    }
    if (capacity == 0U && shared_floor != 0U) {
        return ResultType{time_error(errors::invalid_request)};
    }

    PartitionLedger ledger{};
    ledger.capabilities_ = capabilities;
    ledger.capacity_ = capacity;
    ledger.shared_floor_ = shared_floor;
    return ResultType{ledger};
}

std::uint32_t PartitionLedger::available() const noexcept {
    // Saturating rather than wrapping. reserved_ can never exceed the reservable
    // pool by construction, but a caller comparing against a wrapped value would
    // be handed four billion units by an arithmetic slip, and that failure would
    // be silent.
    if (capacity_ <= shared_floor_) {
        return 0U;
    }
    const auto reservable = capacity_ - shared_floor_;
    if (reserved_ >= reservable) {
        return 0U;
    }
    return reservable - reserved_;
}

std::size_t PartitionLedger::index_of(os::core::PrincipalId principal) const noexcept {
    for (std::size_t index = 0U; index < holder_count_; ++index) {
        if (holders_[index].principal == principal) {
            return index;
        }
    }
    return max_partition_holders;
}

bool PartitionLedger::holds(os::core::PrincipalId principal) const noexcept {
    return index_of(principal) != max_partition_holders;
}

std::uint32_t PartitionLedger::units_for(os::core::PrincipalId principal) const noexcept {
    const auto index = index_of(principal);
    return index == max_partition_holders ? 0U : holders_[index].units;
}

os::core::Result<void> PartitionLedger::reserve(
    os::core::PrincipalId principal,
    std::uint32_t units) noexcept {
    // The unset principal is not an identity, and granting to it would create a
    // reservation nobody can be held responsible for or reclaim from.
    if (!os::core::valid_principal(principal)) {
        return os::core::Result<void>{time_error(errors::invalid_request)};
    }
    if (units == 0U) {
        return os::core::Result<void>{time_error(errors::invalid_request)};
    }
    if (capacity_ == 0U || capabilities_ == 0U) {
        return os::core::Result<void>{time_error(errors::unsupported_platform)};
    }
    // No implicit top-up. A second reservation must be an explicit release and
    // reserve, so a caller cannot accumulate the resource by repeating a call
    // whose failure it never checked.
    if (holds(principal)) {
        return os::core::Result<void>{time_error(errors::already_holding)};
    }
    if (holder_count_ >= max_partition_holders) {
        return os::core::Result<void>{time_error(errors::too_many_holders)};
    }
    if (units > available()) {
        // Distinguishing these two tells the caller whether waiting could ever
        // help. Starving the shared remainder never becomes possible.
        return os::core::Result<void>{
            units > (capacity_ - shared_floor_)
                ? time_error(errors::would_starve_shared)
                : time_error(errors::insufficient_capacity)};
    }

    holders_[holder_count_] = PartitionReservation{principal, units};
    ++holder_count_;
    reserved_ += units;
    return os::core::Result<void>{};
}

os::core::Result<void> PartitionLedger::release(os::core::PrincipalId principal) noexcept {
    const auto index = index_of(principal);
    if (index == max_partition_holders) {
        return os::core::Result<void>{time_error(errors::not_holding)};
    }
    (void)reclaim(principal);
    return os::core::Result<void>{};
}

std::uint32_t PartitionLedger::reclaim(os::core::PrincipalId principal) noexcept {
    const auto index = index_of(principal);
    if (index == max_partition_holders) {
        return 0U;
    }

    const auto units = holders_[index].units;

    // Compact by moving the last entry into the hole. The vacated slot is then
    // cleared rather than left holding a stale principal and unit count: a
    // reclaimed reservation must not be recoverable by reading past the live
    // range, and a duplicated identity beyond holder_count_ would make a future
    // index_of ambiguous if the bound were ever wrong.
    holders_[index] = holders_[holder_count_ - 1U];
    holders_[holder_count_ - 1U] = PartitionReservation{};
    --holder_count_;
    reserved_ -= units;
    return units;
}

} // namespace os::time
