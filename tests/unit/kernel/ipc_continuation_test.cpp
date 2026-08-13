#include <cstdlib>

#include <os/kernel/ipc_continuation.hpp>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
}

int main() {
    os::kernel::AddressSpaceEpochAuthority epochs{};
    auto epoch = epochs.acquire();
    require(static_cast<bool>(epoch));

    os::kernel::IpcContinuationTable continuations{};
    require(static_cast<bool>(continuations.arm(1U, epoch.value(), 0x4000U, epochs)));
    require(static_cast<bool>(continuations.arm_receive(2U, epoch.value(), 17U, 0x5000U, epochs)));
    require(continuations.send_armed(1U));
    require(continuations.receive_armed(2U));
    require(continuations.count() == 2U);

    auto send = continuations.take(1U, epoch.value(), epochs);
    auto receive = continuations.take_receive(2U, epoch.value(), epochs);
    require(send && receive);
    require(send.value().exchange_address == 0x4000U);
    require(receive.value().endpoint_capability == 17U);
    require(receive.value().exchange_address == 0x5000U);
    require(continuations.count() == 0U);

    // Both continuation classes are generation-bound. Retirement consumes the
    // old authority even when a later process incarnation reuses the same VA.
    require(static_cast<bool>(continuations.arm_receive(2U, epoch.value(), 17U, 0x5000U, epochs)));
    auto retiring = epochs.begin_retire(epoch.value());
    require(static_cast<bool>(retiring));
    require(!continuations.take_receive(2U, epoch.value(), epochs));
    require(!continuations.receive_armed(2U));
    require(continuations.count() == 0U);
    require(static_cast<bool>(epochs.complete_retire(retiring.value())));

    auto replacement = epochs.acquire();
    require(static_cast<bool>(replacement));
    require(static_cast<bool>(continuations.arm_receive(2U, replacement.value(), 19U, 0x5000U, epochs)));
    auto fresh = continuations.take_receive(2U, replacement.value(), epochs);
    require(static_cast<bool>(fresh));
    require(fresh.value().endpoint_capability == 19U);

    // Thread teardown is sufficient cleanup; neither blocked syscall class
    // relies on userspace running a destructor/cleanup path.
    require(static_cast<bool>(continuations.arm(3U, replacement.value(), 0x6000U, epochs)));
    require(static_cast<bool>(continuations.arm_receive(3U, replacement.value(), 21U, 0x7000U, epochs)));
    require(continuations.count() == 2U);
    continuations.release_thread(3U);
    require(continuations.count() == 0U);

    return 0;
}
