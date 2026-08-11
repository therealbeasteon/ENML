#include <cstdint>
#include <cstdio>

#include <os/time/protection.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "time protection: %s\n", what);
    }
    return condition;
}

constexpr os::core::PrincipalId principal_of(std::uint64_t value) {
    return os::core::PrincipalId{0x54494D4550524F54ULL, value};
}

constexpr std::uint32_t cache_partitioning =
    static_cast<std::uint32_t>(os::time::TimeProtectionCapability::partitioned_cache);

} // namespace

int main() {
    // A default ledger grants nothing, which is what a caller who forgot to
    // initialise must get.
    {
        os::time::PartitionLedger ledger{};
        if (!check(ledger.capacity() == 0U, "default ledger had capacity")) return 1;
        if (!check(ledger.available() == 0U, "default ledger had units available")) return 1;
        if (!check(!ledger.reserve(principal_of(1U), 1U),
                   "default ledger granted a reservation")) return 1;
    }

    // Construction rules.
    {
        if (!check(!os::time::PartitionLedger::create(0x8000'0000U, 16U, 4U),
                   "unknown capability bit accepted")) return 1;
        // Capacity with no mechanism would hand out reservations that partition
        // nothing, and the holder would believe it was isolated.
        if (!check(!os::time::PartitionLedger::create(0U, 16U, 4U),
                   "capacity accepted with no capability")) return 1;
        // A resource with nothing left over is captured, not shared.
        if (!check(!os::time::PartitionLedger::create(cache_partitioning, 16U, 0U),
                   "zero shared floor accepted")) return 1;
        if (!check(!os::time::PartitionLedger::create(cache_partitioning, 16U, 16U),
                   "shared floor equal to capacity accepted")) return 1;
        if (!check(!os::time::PartitionLedger::create(cache_partitioning, 16U, 17U),
                   "shared floor above capacity accepted")) return 1;
        // A platform providing nothing is legitimate so long as it claims
        // nothing, exactly as with boot and device capabilities.
        auto none = os::time::PartitionLedger::create(0U, 0U, 0U);
        if (!check(static_cast<bool>(none), "empty platform rejected")) return 1;
        if (!check(none.value().available() == 0U, "empty platform offered units")) return 1;
    }

    auto created = os::time::PartitionLedger::create(cache_partitioning, 16U, 4U);
    if (!check(static_cast<bool>(created), "ledger creation failed")) return 1;
    auto ledger = created.value();

    if (!check(ledger.available() == 12U, "wrong initial availability")) return 1;
    if (!check(ledger.provides(os::time::TimeProtectionCapability::partitioned_cache),
               "capability lost")) return 1;
    if (!check(!ledger.provides(os::time::TimeProtectionCapability::partitioned_tlb),
               "capability invented")) return 1;

    // Requests that must be refused rather than partially satisfied.
    if (!check(!ledger.reserve(os::core::PrincipalId{}, 1U),
               "unset principal granted a reservation")) return 1;
    if (!check(!ledger.reserve(principal_of(1U), 0U), "zero-unit reservation accepted")) return 1;
    if (!check(!ledger.reserve(principal_of(1U), 13U),
               "reservation eating the shared floor accepted")) return 1;
    if (!check(!ledger.reserve(principal_of(1U), 16U),
               "reservation of the whole resource accepted")) return 1;
    if (!check(ledger.available() == 12U, "a refused request changed the ledger")) return 1;

    // A granted reservation, and its accounting.
    if (!check(static_cast<bool>(ledger.reserve(principal_of(1U), 5U)),
               "valid reservation refused")) return 1;
    if (!check(ledger.reserved() == 5U, "wrong reserved total")) return 1;
    if (!check(ledger.available() == 7U, "wrong availability after reserving")) return 1;
    if (!check(ledger.holds(principal_of(1U)), "holder not recorded")) return 1;
    if (!check(ledger.units_for(principal_of(1U)) == 5U, "wrong units recorded")) return 1;
    if (!check(!ledger.holds(principal_of(2U)), "unrelated principal reported as holder")) return 1;

    // No implicit top-up: a repeated request is an error, not an accumulation.
    if (!check(!ledger.reserve(principal_of(1U), 1U), "duplicate reservation accepted")) return 1;
    if (!check(ledger.reserved() == 5U, "duplicate reservation changed the total")) return 1;

    // Exhausting the reservable pool, but never the floor.
    if (!check(static_cast<bool>(ledger.reserve(principal_of(2U), 7U)),
               "reservation of the remainder refused")) return 1;
    if (!check(ledger.available() == 0U, "availability wrong at exhaustion")) return 1;
    if (!check(!ledger.reserve(principal_of(3U), 1U),
               "reservation granted with nothing available")) return 1;
    if (!check(ledger.reserved() + ledger.shared_floor() <= ledger.capacity(),
               "reservations exceeded capacity")) return 1;

    // Reclamation on death. This is the property the whole type exists for: a
    // principal that no longer exists must hold nothing.
    if (!check(ledger.reclaim(principal_of(1U)) == 5U, "reclaim returned wrong units")) return 1;
    if (!check(!ledger.holds(principal_of(1U)), "reclaimed principal still holds")) return 1;
    if (!check(ledger.units_for(principal_of(1U)) == 0U, "reclaimed units still recorded")) return 1;
    if (!check(ledger.reserved() == 7U, "reclaim did not return the units")) return 1;
    if (!check(ledger.available() == 5U, "availability wrong after reclaim")) return 1;
    // The surviving holder must be untouched by the compaction.
    if (!check(ledger.units_for(principal_of(2U)) == 7U,
               "compaction disturbed a surviving holder")) return 1;

    // Reclaiming a principal that holds nothing is a normal answer, not a
    // fault: a supervisor tearing down a dead process must not have an error to
    // ignore here.
    if (!check(ledger.reclaim(principal_of(99U)) == 0U, "reclaim of a non-holder returned units"))
        return 1;
    if (!check(ledger.reserved() == 7U, "reclaim of a non-holder changed the total")) return 1;

    // Voluntary release distinguishes the non-holder case.
    if (!check(!ledger.release(principal_of(99U)), "release of a non-holder succeeded")) return 1;
    if (!check(static_cast<bool>(ledger.release(principal_of(2U))), "release of a holder failed"))
        return 1;
    if (!check(ledger.reserved() == 0U, "release did not return the units")) return 1;
    if (!check(ledger.holder_count() == 0U, "release left a holder")) return 1;
    if (!check(ledger.available() == 12U, "availability wrong after full release")) return 1;

    // The holder table has a ceiling, and reaching it is a refusal rather than
    // an overwrite.
    {
        auto bounded = os::time::PartitionLedger::create(cache_partitioning, 64U, 8U);
        if (!check(static_cast<bool>(bounded), "bounded ledger creation failed")) return 1;
        auto table = bounded.value();
        for (std::uint64_t index = 0U; index < os::time::max_partition_holders; ++index) {
            if (!check(static_cast<bool>(table.reserve(principal_of(index + 1U), 1U)),
                       "reservation within the ceiling refused")) return 1;
        }
        if (!check(table.holder_count() == os::time::max_partition_holders,
                   "wrong holder count at the ceiling")) return 1;
        if (!check(!table.reserve(principal_of(1000U), 1U),
                   "reservation accepted past the holder ceiling")) return 1;
        if (!check(table.reserved() == os::time::max_partition_holders,
                   "refused reservation changed the total")) return 1;

        // Reclaiming the first holder must leave every other one intact, which
        // is where a compaction that moves entries can go wrong.
        if (!check(table.reclaim(principal_of(1U)) == 1U, "reclaim returned wrong units")) return 1;
        for (std::uint64_t index = 1U; index < os::time::max_partition_holders; ++index) {
            if (!check(table.units_for(principal_of(index + 1U)) == 1U,
                       "compaction lost a holder")) return 1;
        }
        if (!check(!table.holds(principal_of(1U)), "compaction left the reclaimed holder")) return 1;
    }

    return 0;
}
