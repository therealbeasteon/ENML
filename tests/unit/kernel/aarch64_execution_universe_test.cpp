#include <os/kernel/aarch64_execution_universe.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    PreemptionResult good{};
    good.next = 7U;
    good.translation = ProcessTranslationBinding{
        .thread = 7U,
        .epoch = AddressSpaceEpoch{.slot = 2U, .generation = 9U, .asid = 3U},
        .root_physical = 0x4000ULL,
    };
    good.deadline = SchedulerDeadline{
        .generation = 11U,
        .absolute_nanoseconds = 2'000'000ULL,
        .active = true,
    };

    auto plan = prepare_execution_universe(good);
    require(plan);
    require(plan.value().thread == 7U);
    require(plan.value().epoch == good.translation.epoch);
    require(plan.value().root_physical == 0x4000ULL);
    require(plan.value().deadline.generation == 11U);

    auto wrong_thread = good;
    wrong_thread.next = 8U;
    require(!prepare_execution_universe(wrong_thread));

    auto invalid_epoch = good;
    invalid_epoch.translation.epoch = AddressSpaceEpoch{};
    require(!prepare_execution_universe(invalid_epoch));

    auto missing_root = good;
    missing_root.translation.root_physical = 0ULL;
    require(!prepare_execution_universe(missing_root));

    auto missing_deadline_generation = good;
    missing_deadline_generation.deadline = SchedulerDeadline{};
    require(!prepare_execution_universe(missing_deadline_generation));

    // Tickless cancellation is still a real generation-scoped decision.
    auto no_timer = good;
    no_timer.deadline = SchedulerDeadline{.generation = 12U, .active = false};
    auto no_timer_plan = prepare_execution_universe(no_timer);
    require(no_timer_plan);
    require(!no_timer_plan.value().deadline.active);

    return 0;
}
