#include <os/kernel/aarch64_preemption.hpp>
#include <os/kernel/aarch64_translation_root_sealer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 12U * 4096U> memory{};
    const auto begin = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory.data()));
    EarlyPageArena arena{begin, begin + memory.size()};
    EarlyStage1Builder builder_a{arena};
    EarlyStage1Builder builder_b{arena};
    EarlyStage1Builder builder_stale{arena};
    require(builder_a.initialize());
    require(builder_b.initialize());
    require(builder_stale.initialize());
    auto root_a = TranslationRootSealer::seal(builder_a);
    auto root_b = TranslationRootSealer::seal(builder_b);
    auto root_stale = TranslationRootSealer::seal(builder_stale);
    require(root_a && root_b && root_stale);

    AddressSpaceEpochAuthority epochs{};
    auto epoch_a = epochs.acquire();
    auto epoch_b = epochs.acquire();
    require(epoch_a && epoch_b);

    ProcessTranslationTable translations{};
    require(translations.bind(1U, epoch_a.value(), root_a.value(), epochs));
    require(translations.bind(2U, epoch_b.value(), root_b.value(), epochs));

    Scheduler scheduler{};
    require(scheduler.admit(1U, 4U));
    require(scheduler.admit(2U, 4U));

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
    require(preemption.admit_frame(1U, a));
    require(preemption.admit_frame(2U, b));

    ExceptionFrame live{};
    auto start = preemption.start(scheduler, translations, epochs, 1'000'000U, live);
    require(start);
    require(start.value().translation.root_physical == root_a.value().root_physical());
    live.x[0] = 0xA55AU;

    auto to_b = preemption.on_timer(
        scheduler, translations, epochs, start.value().deadline,
        start.value().deadline.absolute_nanoseconds, live);
    require(to_b);
    require(to_b.value().next == 2U);
    require(to_b.value().translation.root_physical == root_b.value().root_physical());
    require(live.x[0] == 0xB0U);

    live.x[0] = 0xB55BU;
    auto to_a = preemption.on_timer(
        scheduler, translations, epochs, to_b.value().deadline,
        to_b.value().deadline.absolute_nanoseconds, live);
    require(to_a);
    require(to_a.value().next == 1U);
    require(live.x[0] == 0xA55AU);

    require(scheduler.update(2U, false, 4U));
    auto final = preemption.on_timer(
        scheduler, translations, epochs, to_a.value().deadline,
        to_a.value().deadline.absolute_nanoseconds, live);
    require(final);
    require(!final.value().deadline.active);

    AddressSpaceEpochAuthority stale_epochs{};
    auto stale_epoch = stale_epochs.acquire();
    require(stale_epoch);
    ProcessTranslationTable stale_translations{};
    require(stale_translations.bind(7U, stale_epoch.value(), root_stale.value(), stale_epochs));
    require(stale_epochs.begin_retire(stale_epoch.value()));

    Scheduler stale_scheduler{};
    require(stale_scheduler.admit(7U, 4U));
    PreemptionCoordinator stale_preemption{};
    ExceptionFrame stale_frame{};
    stale_frame.elr_el1 = 0x7000U;
    stale_frame.sp_el0 = 0xA000U;
    stale_frame.spsr_el1 = 0U;
    require(stale_preemption.admit_frame(7U, stale_frame));

    ExceptionFrame untouched{};
    untouched.elr_el1 = 0xDEADU;
    untouched.sp_el0 = 0xBEEFU;
    untouched.x[0] = 0xCAFEU;
    const auto before = untouched;
    auto rejected = stale_preemption.start(
        stale_scheduler, stale_translations, stale_epochs, 2'000'000U, untouched);
    require(!rejected);
    require(rejected.error().code == preemption_errors::translation_unavailable);
    require(untouched.elr_el1 == before.elr_el1);
    require(untouched.sp_el0 == before.sp_el0);
    require(untouched.x[0] == before.x[0]);

    return 0;
}
