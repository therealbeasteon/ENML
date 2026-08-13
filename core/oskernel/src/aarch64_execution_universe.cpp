#include <os/kernel/aarch64_execution_universe.hpp>

#include <os/core/error.hpp>

namespace os::kernel::aarch64 {
namespace {
[[nodiscard]] constexpr os::core::Error execution_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

os::core::Result<ExecutionUniversePlan> prepare_execution_universe(
    const PreemptionResult& result) noexcept {
    if (!result.translation.valid() || result.next == invalid_thread ||
        result.next != result.translation.thread ||
        result.deadline.generation == 0ULL) {
        return execution_error(execution_universe_errors::invalid_plan);
    }

    return ExecutionUniversePlan{
        .thread = result.next,
        .epoch = result.translation.epoch,
        .root_physical = result.translation.root_physical,
        .deadline = result.deadline,
    };
}

} // namespace os::kernel::aarch64
