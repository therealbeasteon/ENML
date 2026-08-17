#include <os/kernel/syscall_outcome.hpp>

namespace os::kernel::aarch64 {
namespace {

// x0 and x1, and deliberately not x0..x3.
//
// The first version of this cleared all four argument registers, on the
// reasoning that a call's results occupy the registers its arguments arrived
// in. Converting the call sites showed that to be wrong in a way that would
// have destroyed data: **x2 and x3 are not result registers for any call in
// the surface.** They carry *out-of-band* delivery - the interrupt service a
// driver is handed on its resume (x2 assertions, x3 saturated), and the region
// a pager is asked about (x2) - both of which ride back on a wakeup precisely
// so that being told costs no syscall.
//
// So clearing them would have made the property "was an answer written after
// out-of-band data?" load-bearing across two files, and the answer today is
// only accidentally safe: `complete_after_switch` happens to call
// `complete_ipc_current` before `complete_interrupt_current`. Reversing those
// two lines would have silently emptied every interrupt service.
//
// Nothing in the surface returns more than two values, so clearing x0 and x1
// clears every register a result can occupy. The property that mattered is
// kept - a caller cannot get its own argument back as a result - and the
// ordering hazard is removed rather than documented.
inline constexpr std::size_t max_result_registers = 2U;
static_assert(max_result_registers <= max_call_arguments);

constexpr void clear_results(ExceptionFrame& frame) noexcept {
    for (std::size_t index = 0U; index < max_result_registers; ++index) {
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
