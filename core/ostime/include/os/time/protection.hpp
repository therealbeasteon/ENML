#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>

// Time protection: the OS half of defending against timing side channels.
//
// Timing attacks work by sharing a resource. Recovering an AES key from a phone
// by watching cache evictions needs no privileges at all - it needs only that
// the attacker and the victim contend for the same cache sets. Writing
// constant-time code narrows one path to one secret; it does nothing about the
// sharing itself, and it cannot, because sharing is not a property of a
// function.
//
// Removing the sharing means partitioning a micro-architectural resource -
// locking cache ways to a principal, reserving a TLB partition - and that is
// necessarily an operating system job. The platform supplies a mechanism; the
// OS decides who gets it, refuses when it cannot be given, and takes it back.
// The reference design for this names exactly three responsibilities, and the
// third is the one that is a security bug rather than a feature:
//
//   1. grant partitioning to the principals that need it,
//   2. handle an acquisition that cannot be satisfied,
//   3. reclaim partitions held by processes that were killed.
//
// This file is (1), (2) and (3). It is deliberately platform-independent
// bookkeeping: what a "unit" is - a cache way, a colour, a TLB entry - is the
// platform port's business, and the accounting rules do not change with the
// answer. Nothing here programs hardware.
//
// Two rules in the ledger below are security properties rather than
// housekeeping, and both come from the same observation - that a partitioning
// mechanism is a bounded resource, and bounded resources handed out on request
// are denial-of-service surfaces:
//
//   - A reservation may never consume the last of the shared remainder. A
//     principal that reserved every way of a cache would not be isolating
//     itself, it would be evicting everybody else permanently.
//
//   - A failed reservation is an error the caller must handle. It is never
//     silently downgraded to running shared, because code that believes it is
//     partitioned and is not will make exactly the assumptions the partition
//     was supposed to justify.
namespace os::time {

// A ledger tracking more distinct principals than this is not describing a
// small set of privileged, timing-sensitive components any more. The ceiling is
// a rejection criterion, not a buffer size.
inline constexpr std::size_t max_partition_holders = 8U;

namespace errors {
inline constexpr std::uint32_t unsupported_platform = 1U;
inline constexpr std::uint32_t invalid_request = 2U;
inline constexpr std::uint32_t insufficient_capacity = 3U;
inline constexpr std::uint32_t would_starve_shared = 4U;
inline constexpr std::uint32_t already_holding = 5U;
inline constexpr std::uint32_t not_holding = 6U;
inline constexpr std::uint32_t too_many_holders = 7U;
} // namespace errors

// What partitioning the platform actually provides.
//
// As with the boot-state capability set and the device DMA capability, absence
// is a fact rather than a failure: most platforms provide none of this, and a
// hardware-neutral OS has to say so plainly instead of assuming otherwise. Zero
// is never a valid single capability.
enum class TimeProtectionCapability : std::uint32_t {
    // Cache ways or colours can be reserved to a principal.
    partitioned_cache = 1U << 0U,
    // Individual lines can be locked in and pinned against eviction.
    cache_line_locking = 1U << 1U,
    // The TLB can be partitioned the same way.
    partitioned_tlb = 1U << 2U,
    // Micro-architectural state is flushed on a context switch, so history does
    // not cross a principal boundary even without partitioning.
    flush_on_context_switch = 1U << 3U,
    // Variable-latency arithmetic - division and modulo, most often - can be
    // put in a fixed-latency mode.
    deterministic_arithmetic = 1U << 4U,
};

inline constexpr std::uint32_t known_time_protection_capabilities =
    static_cast<std::uint32_t>(TimeProtectionCapability::partitioned_cache) |
    static_cast<std::uint32_t>(TimeProtectionCapability::cache_line_locking) |
    static_cast<std::uint32_t>(TimeProtectionCapability::partitioned_tlb) |
    static_cast<std::uint32_t>(TimeProtectionCapability::flush_on_context_switch) |
    static_cast<std::uint32_t>(TimeProtectionCapability::deterministic_arithmetic);

struct PartitionReservation final {
    os::core::PrincipalId principal {};
    std::uint32_t units {0};

    [[nodiscard]] friend bool
    operator==(const PartitionReservation&, const PartitionReservation&) = default;
};

// Bookkeeping for a partitionable micro-architectural resource.
//
// A default-constructed ledger has no capacity and no capabilities, so every
// reservation against it fails. That is the same discipline as the rest of the
// tree: forgetting to initialise gives you the answer that grants nothing, not
// an unbounded resource.
class PartitionLedger final {
public:
    PartitionLedger() noexcept = default;

    // capacity is the total number of partitionable units the platform reports.
    // shared_floor is how many must remain unreserved for everything that did
    // not ask - it must be at least one, because a resource with nothing left
    // over is not shared, it is captured.
    [[nodiscard]] static os::core::Result<PartitionLedger> create(
        std::uint32_t capabilities,
        std::uint32_t capacity,
        std::uint32_t shared_floor) noexcept;

    [[nodiscard]] std::uint32_t capabilities() const noexcept { return capabilities_; }

    [[nodiscard]] bool provides(TimeProtectionCapability capability) const noexcept {
        return (capabilities_ & static_cast<std::uint32_t>(capability)) != 0U;
    }

    [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::uint32_t shared_floor() const noexcept { return shared_floor_; }
    [[nodiscard]] std::uint32_t reserved() const noexcept { return reserved_; }

    // Units a further reservation could still take. Saturates at zero rather
    // than wrapping, so a caller comparing against it cannot be handed a huge
    // number by an arithmetic slip.
    [[nodiscard]] std::uint32_t available() const noexcept;

    [[nodiscard]] std::size_t holder_count() const noexcept { return holder_count_; }
    [[nodiscard]] bool holds(os::core::PrincipalId principal) const noexcept;
    [[nodiscard]] std::uint32_t units_for(os::core::PrincipalId principal) const noexcept;

    // Grants units to a principal. Fails - rather than granting fewer - if the
    // request cannot be met in full, if it would eat into the shared floor, if
    // the principal already holds a reservation, or if the table is full.
    [[nodiscard]] os::core::Result<void> reserve(
        os::core::PrincipalId principal,
        std::uint32_t units) noexcept;

    // Voluntary release by a principal that holds a reservation.
    [[nodiscard]] os::core::Result<void> release(os::core::PrincipalId principal) noexcept;

    // Involuntary reclamation, called when a principal's process dies.
    //
    // Deliberately not a Result: a supervisor tearing down a dead process must
    // not have an error to ignore on this path, and "it held nothing" is a
    // normal answer rather than a fault. Returns the number of units taken
    // back, which is zero when the principal held none.
    std::uint32_t reclaim(os::core::PrincipalId principal) noexcept;

private:
    [[nodiscard]] std::size_t index_of(os::core::PrincipalId principal) const noexcept;

    std::uint32_t capabilities_ {0};
    std::uint32_t capacity_ {0};
    std::uint32_t shared_floor_ {0};
    std::uint32_t reserved_ {0};
    std::size_t holder_count_ {0};
    std::array<PartitionReservation, max_partition_holders> holders_ {};
};

} // namespace os::time
