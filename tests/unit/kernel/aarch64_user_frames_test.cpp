#include <os/kernel/aarch64_user_frames.hpp>

#include <cstdlib>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    UserFrameTable table{};
    ExceptionFrame a{};
    a.elr_el1 = 0x1000U;
    a.sp_el0 = 0x8000U;
    a.spsr_el1 = 0U; // EL0t
    a.x[0] = 0xAAAAU;
    ExceptionFrame b{};
    b.elr_el1 = 0x2000U;
    b.sp_el0 = 0x9000U;
    b.spsr_el1 = 0U;
    b.x[0] = 0xBBBBU;

    require(static_cast<bool>(table.admit(1U, a)));
    require(static_cast<bool>(table.admit(2U, b)));
    require(table.size() == 2U);

    // Capture a changed A frame, then overwrite the same live IRQ frame with B.
    ExceptionFrame live = a;
    live.x[0] = 0xA55AU;
    require(static_cast<bool>(table.capture(1U, live)));
    require(static_cast<bool>(table.restore(2U, live)));
    require(live.elr_el1 == b.elr_el1);
    require(live.sp_el0 == b.sp_el0);
    require(live.x[0] == 0xBBBBU);

    require(static_cast<bool>(table.restore(1U, live)));
    require(live.x[0] == 0xA55AU);

    ExceptionFrame privileged = a;
    privileged.spsr_el1 = 0x5U; // EL1h; scheduler must never mint this from EL0 state.
    auto bad = table.admit(3U, privileged);
    require(!bad);
    require(bad.error().code == user_frame_errors::invalid_el0_frame);

    require(static_cast<bool>(table.retire(2U)));
    require(!table.contains(2U));
    require(table.size() == 1U);
    return 0;
}
