#include <os/kernel/aarch64_execution_universe.hpp>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_asid.hpp>
#include <os/kernel/machine.hpp>

#if !defined(__aarch64__)
#error "aarch64_execution_universe_machine.cpp must only be compiled for AArch64"
#endif

namespace os::kernel::aarch64 {
namespace {
[[nodiscard]] constexpr os::core::Error execution_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

os::core::Result<void> commit_execution_universe(
    const ExecutionUniversePlan& plan,
    std::uint64_t now_nanoseconds) noexcept {
    if (!plan.valid()) return execution_error(execution_universe_errors::invalid_plan);

    if (plan.deadline.active) {
        if (plan.deadline.absolute_nanoseconds <= now_nanoseconds) {
            return execution_error(execution_universe_errors::stale_deadline);
        }
        const auto delta = plan.deadline.absolute_nanoseconds - now_nanoseconds;
        if (!machine_set_timer(delta)) {
            return execution_error(execution_universe_errors::machine_commit_failed);
        }
    } else if (!machine_cancel_timer()) {
        return execution_error(execution_universe_errors::machine_commit_failed);
    }

    auto translation = install_process_translation(plan.root_physical, plan.epoch);
    if (!translation) {
        // Do not leave a freshly armed deadline able to fire after a failed
        // translation commit. The caller treats this as an invariant failure.
        (void)machine_cancel_timer();
        return execution_error(execution_universe_errors::machine_commit_failed);
    }
    return {};
}

} // namespace os::kernel::aarch64
