#include <cstdlib>

#include <os/kernel/aarch64_user_copy_guard.hpp>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
constexpr std::uint64_t current_el_data_abort = 0x25ULL << 26U;
}

int main() {
    using namespace os::kernel::aarch64;

    ExceptionFrame frame{};
    frame.esr_el1 = current_el_data_abort;
    frame.elr_el1 = 0x2000ULL;
    frame.far_el1 = 0x1000ULL;

    require(arm_user_copy_fault_guard(0x1000ULL, 0x1001ULL, 0x2000ULL, 0x3000ULL));

    // Exact range but unrelated privileged faulting instruction: never recover.
    frame.elr_el1 = 0x2004ULL;
    require(!recover_user_copy_fault(frame));
    disarm_user_copy_fault_guard();

    // Exact instruction but address outside the authorized byte: never recover.
    require(arm_user_copy_fault_guard(0x1000ULL, 0x1001ULL, 0x2000ULL, 0x3000ULL));
    frame.elr_el1 = 0x2000ULL;
    frame.far_el1 = 0x1001ULL;
    require(!recover_user_copy_fault(frame));
    disarm_user_copy_fault_guard();

    // A non-Data-Abort synchronous exception is not a copy fault.
    require(arm_user_copy_fault_guard(0x1000ULL, 0x1001ULL, 0x2000ULL, 0x3000ULL));
    frame.far_el1 = 0x1000ULL;
    frame.esr_el1 = 0ULL;
    require(!recover_user_copy_fault(frame));
    disarm_user_copy_fault_guard();

    // Only the exact tuple is recoverable, and recovery consumes the guard.
    require(arm_user_copy_fault_guard(0x1000ULL, 0x1001ULL, 0x2000ULL, 0x3000ULL));
    frame.esr_el1 = current_el_data_abort;
    frame.elr_el1 = 0x2000ULL;
    frame.far_el1 = 0x1000ULL;
    require(recover_user_copy_fault(frame));
    require(frame.elr_el1 == 0x3000ULL);
    require(!recover_user_copy_fault(frame));

    return 0;
}
