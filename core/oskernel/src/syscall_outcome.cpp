#include <os/kernel/syscall_outcome.hpp>

namespace os::kernel::aarch64 {
namespace {

// Result registers are x0..x3, which is max_call_arguments - a call's results
// occupy the same registers its arguments arrived in. Cleared on every answer
// so nothing a caller passed in can come back out looking like something the
// kernel decided.
constexpr void clear_results(ExceptionFrame& frame) noexcept {
    for (std::size_t index = 0U; index < max_call_arguments; ++index) {
        frame.x[index] = 0ULL;
    }
}

} // namespace

void answer(ExceptionFrame& frame) noexcept {
    clear_results(frame);
    frame.x[outcome_register] = outcome_answered;
}

void answer(ExceptionFrame& frame, std::uint64_t x0) noexcept {
    clear_results(frame);
    frame.x[0] = x0;
    frame.x[outcome_register] = outcome_answered;
}

void answer(ExceptionFrame& frame, std::uint64_t x0, std::uint64_t x1) noexcept {
    clear_results(frame);
    frame.x[0] = x0;
    frame.x[1] = x1;
    frame.x[outcome_register] = outcome_answered;
}

void refuse(ExceptionFrame& frame, os::core::Error error) noexcept {
    clear_results(frame);
    frame.x[0] = encode_call_error(error);
    frame.x[outcome_register] = outcome_refused;
}

bool answered(const ExceptionFrame& frame) noexcept {
    return frame.x[outcome_register] == outcome_answered ||
           frame.x[outcome_register] == outcome_refused;
}

void clear_outcome(ExceptionFrame& frame) noexcept {
    frame.x[outcome_register] = 0ULL;
}

} // namespace os::kernel::aarch64
