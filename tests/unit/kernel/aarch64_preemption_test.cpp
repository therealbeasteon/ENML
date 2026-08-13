#include <os/kernel/aarch64_execution_universe.hpp>
#include <os/kernel/aarch64_kernel_mapping_manifest.hpp>
#include <os/kernel/aarch64_preemption.hpp>
#include <os/kernel/aarch64_translation_root_sealer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    KernelMappingManifest manifest{};
    require(static_cast<bool>(manifest.add(KernelMappingManifestEntry{
        .virtual_base = 0x4000ULL,
        .physical_base = 0x4000ULL,
        .length = 0x1000ULL,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::normal,
        .role = KernelMappingRole::ordinary,
    })));
    require(static_cast<bool>(manifest.add(KernelMappingManifestEntry{
        .virtual_base = 0x8000ULL,
        .physical_base = 0x9000ULL,
        .length = 0x2000ULL,
        .permissions = MachinePermissions::read_write,
        .kind = MachineMemoryKind::normal,
        .role = KernelMappingRole::guarded_stack,
    })));
    require(manifest.size() == 2U);
    require(!manifest.add(KernelMappingManifestEntry{
        .virtual_base = 0xA000ULL,
        .physical_base = 0xB000ULL,
        .length = 0x1000ULL,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::normal,
        .role = KernelMappingRole::guarded_stack,
    }));
    require(!manifest.add(KernelMappingManifestEntry{}));

    SchedulerDeadlineAuthority deadline_authority{};
    Decision overflowing{};
    overflowing.thread = 1U;
    overflowing.timer_nanoseconds = 2U;
    const auto generation_before_overflow = deadline_authority.generation();
    auto overflow = deadline_authority.prepare(
        overflowing, std::numeric_limits<std::uint64_t>::max() - 1U);
    require(!overflow);
    require(deadline_authority.generation() == generation_before_overflow);
    require(deadline_authority.current().generation == 0U);

    alignas(4096) std::array<std::byte, 12U * 4096U> memory{};
    const auto begin = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory.data()));
    EarlyPageArena arena{begin, begin + memory.size()};
    EarlyStage1Builder builder_a{arena};
    EarlyStage1Builder builder_b{arena};
    EarlyStage1Builder builder_stale{arena};
    require(static_cast<bool>(builder_a.initialize()));
    require(static_cast<bool>(builder_b.initialize()));
    require(static_cast<bool>(builder_stale.initialize()));
    auto root_a = TranslationRootSealer::seal(builder_a);
    auto root_b = TranslationRootSealer::seal(builder_b);
    auto root_stale = TranslationRootSealer::seal(builder_stale);
    require(root_a && root_b && root_stale);

    AddressSpaceEpochAuthority epochs{};
    auto epoch_a = epochs.acquire();
    auto epoch_b = epochs.acquire();
    require(epoch_a && epoch_b);

    ProcessTranslationTable translations{};
    require(static_cast<bool>(translations.bind(1U, epoch_a.value(), root_a.value(), epochs)));
    require(static_cast<bool>(translations.bind(2U, epoch_b.value(), root_b.value(), epochs)));

    Scheduler scheduler{};
    require(static_cast<bool>(scheduler.admit(1U, 4U)));
    require(static_cast<bool>(scheduler.admit(2U, 4U)));

    ExceptionFrame a{};
    a.elr_el1 = 0x1000U;
    a.sp_el0 = 0x8000U;
    a.spsr_el1 = 0U;
    a.x[0] = 0xA0U;
    ExceptionFrame b{};
    b.elr_el1 = 0x2000U;
    b.sp_el0 = 0x9000U;
    b.spsr_el1 = 0U;
    b.x[0] = 0xB0U;

    PreemptionCoordinator preemption{};
    require(static_cast<bool>(preemption.admit_frame(1U, a)));
    require(static_cast<bool>(preemption.admit_frame(2U, b)));

    ExceptionFrame live{};
    auto start = preemption.start(scheduler, translations, epochs, 1'000'000U, live);
    require(static_cast<bool>(start));
    require(start.value().translation.root_physical == root_a.value().root_physical());
    auto start_plan = prepare_execution_universe(start.value());
    require(static_cast<bool>(start_plan));
    require(start_plan.value().thread == 1U);
    require(start_plan.value().epoch == epoch_a.value());
    require(start_plan.value().root_physical == root_a.value().root_physical());
    live.x[0] = 0xA55AU;

    auto to_b = preemption.on_timer(
        scheduler, translations, epochs, start.value().deadline,
        start.value().deadline.absolute_nanoseconds, live);
    require(static_cast<bool>(to_b));
    require(to_b.value().next == 2U);
    require(to_b.value().translation.root_physical == root_b.value().root_physical());
    auto b_plan = prepare_execution_universe(to_b.value());
    require(static_cast<bool>(b_plan));
    require(b_plan.value().thread == 2U);
    require(b_plan.value().epoch == epoch_b.value());
    require(live.x[0] == 0xB0U);

    live.x[0] = 0xB55BU;
    auto to_a = preemption.on_timer(
        scheduler, translations, epochs, to_b.value().deadline,
        to_b.value().deadline.absolute_nanoseconds, live);
    require(static_cast<bool>(to_a));
    require(to_a.value().next == 1U);
    require(live.x[0] == 0xA55AU);

    require(static_cast<bool>(scheduler.update(2U, false, 4U)));
    auto final = preemption.on_timer(
        scheduler, translations, epochs, to_a.value().deadline,
        to_a.value().deadline.absolute_nanoseconds, live);
    require(static_cast<bool>(final));
    require(!final.value().deadline.active);
    auto final_plan = prepare_execution_universe(final.value());
    require(static_cast<bool>(final_plan));
    require(!final_plan.value().deadline.active);

    auto mismatched = final.value();
    mismatched.next = 2U;
    require(!prepare_execution_universe(mismatched));

    AddressSpaceEpochAuthority stale_epochs{};
    auto stale_epoch = stale_epochs.acquire();
    require(static_cast<bool>(stale_epoch));
    ProcessTranslationTable stale_translations{};
    require(static_cast<bool>(
        stale_translations.bind(7U, stale_epoch.value(), root_stale.value(), stale_epochs)));
    require(static_cast<bool>(stale_epochs.begin_retire(stale_epoch.value())));

    Scheduler stale_scheduler{};
    require(static_cast<bool>(stale_scheduler.admit(7U, 4U)));
    PreemptionCoordinator stale_preemption{};
    ExceptionFrame stale_frame{};
    stale_frame.elr_el1 = 0x7000U;
    stale_frame.sp_el0 = 0xA000U;
    stale_frame.spsr_el1 = 0U;
    require(static_cast<bool>(stale_preemption.admit_frame(7U, stale_frame)));

    ExceptionFrame untouched{};
    untouched.elr_el1 = 0xDEADU;
    untouched.sp_el0 = 0xBEEFU;
    untouched.x[0] = 0xCAFEU;
    const auto before = untouched;
    const auto deadline_generation_before = stale_preemption.current_deadline().generation;
    auto rejected = stale_preemption.start(
        stale_scheduler, stale_translations, stale_epochs, 2'000'000U, untouched);
    require(!rejected);
    require(rejected.error().code == preemption_errors::translation_unavailable);
    require(untouched.elr_el1 == before.elr_el1);
    require(untouched.sp_el0 == before.sp_el0);
    require(untouched.x[0] == before.x[0]);
    require(stale_preemption.current_deadline().generation == deadline_generation_before);

    return 0;
}
