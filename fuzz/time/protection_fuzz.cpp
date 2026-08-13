#include <cstddef>
#include <cstdint>

#include <os/time/protection.hpp>

// The partition ledger, driven as a state machine.
//
// Unlike the other targets here this one does not decode a record; it replays
// an attacker-chosen *sequence of operations* and checks that the ledger's
// invariants survive all of them. That is the right shape for this type,
// because its failure mode is not a malformed input but an ordering: a reserve
// racing a reclaim, a release of something never held, a reclamation that
// leaves accounting behind.
//
// The invariants checked after every operation are the ones the rest of the
// system would otherwise have to trust:
//
//   - reserved units always equal the sum of what the holders hold,
//   - the shared remainder is never consumed,
//   - no principal appears twice, so a reclaim cannot leave half a holding,
//   - the unset principal never holds anything.

namespace {

class Cursor final {
public:
    Cursor(const std::uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

    [[nodiscard]] bool empty() const noexcept { return offset_ >= size_; }

    [[nodiscard]] std::uint8_t next() noexcept {
        if (empty()) {
            return 0U;
        }
        return data_[offset_++];
    }

private:
    const std::uint8_t* data_ {nullptr};
    std::size_t size_ {0};
    std::size_t offset_ {0};
};

[[nodiscard]] os::core::PrincipalId principal_of(std::uint8_t value) noexcept {
    // A small principal space, so the sequence collides often: duplicate
    // reservations and reclaims of live holders are the interesting cases, and
    // a wide space would almost never produce them.
    return os::core::PrincipalId{0x54494D4550524F54ULL, static_cast<std::uint64_t>(value % 12U)};
}

void check_invariants(const os::time::PartitionLedger& ledger) noexcept {
    if (ledger.holder_count() > os::time::max_partition_holders) {
        __builtin_trap();
    }

    // Summed over the whole principal space the sequence can produce, which
    // covers every holder that can exist. Going through the public accessor
    // means this checks what callers actually see rather than the private
    // table, so a holder recorded but not reportable would still be caught.
    std::uint32_t summed = 0U;
    for (std::uint8_t value = 0U; value < 12U; ++value) {
        summed += ledger.units_for(principal_of(value));
    }
    if (summed != ledger.reserved()) {
        __builtin_trap();
    }

    // The shared remainder is the whole point: it may never be consumed.
    if (ledger.capacity() != 0U &&
        ledger.reserved() + ledger.shared_floor() > ledger.capacity()) {
        __builtin_trap();
    }

    // available() must be exact and must never wrap into a huge number.
    const std::uint32_t expected = ledger.capacity() > ledger.shared_floor()
        ? (ledger.capacity() - ledger.shared_floor()) - ledger.reserved()
        : 0U;
    if (ledger.available() != expected) {
        __builtin_trap();
    }

    // An identity nobody owns must never hold a reservation, or the units
    // could never be reclaimed from anyone.
    if (ledger.holds(os::core::PrincipalId{})) {
        __builtin_trap();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Cursor cursor{data, size};

    const auto capabilities = static_cast<std::uint32_t>(cursor.next()) &
        os::time::known_time_protection_capabilities;
    const auto capacity = static_cast<std::uint32_t>(cursor.next());
    const auto floor = static_cast<std::uint32_t>(cursor.next());

    auto created = os::time::PartitionLedger::create(capabilities, capacity, floor);
    if (!created) {
        return 0;
    }
    auto ledger = created.value();
    check_invariants(ledger);

    while (!cursor.empty()) {
        const auto operation = cursor.next();
        const auto principal = principal_of(cursor.next());
        const auto units = static_cast<std::uint32_t>(cursor.next());

        switch (operation % 3U) {
        case 0U: {
            const auto before = ledger.reserved();
            const auto granted = static_cast<bool>(ledger.reserve(principal, units));
            if (granted) {
                // A grant must move the total by exactly what was asked for -
                // never a partial grant reported as success.
                if (ledger.reserved() != before + units ||
                    ledger.units_for(principal) != units) {
                    __builtin_trap();
                }
            } else if (ledger.reserved() != before) {
                // A refusal must leave the ledger untouched.
                __builtin_trap();
            }
            break;
        }
        case 1U: {
            const auto held = ledger.units_for(principal);
            const auto released = static_cast<bool>(ledger.release(principal));
            if (released != (held != 0U)) {
                __builtin_trap();
            }
            if (released && ledger.holds(principal)) {
                __builtin_trap();
            }
            break;
        }
        default: {
            // Reclamation is the path a supervisor takes when a process dies.
            // Afterwards the principal must hold nothing, whatever it held
            // before and however many times this is repeated.
            const auto held = ledger.units_for(principal);
            const auto reclaimed = ledger.reclaim(principal);
            if (reclaimed != held || ledger.holds(principal) ||
                ledger.units_for(principal) != 0U) {
                __builtin_trap();
            }
            if (ledger.reclaim(principal) != 0U) {
                __builtin_trap();
            }
            break;
        }
        }

        check_invariants(ledger);
    }

    return 0;
}
