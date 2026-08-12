#include <os/kernel/ipc_continuation.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error continuation_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

IpcContinuationTable::Slot* IpcContinuationTable::slot_for(ThreadId caller) noexcept {
    if (caller == invalid_thread) return nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.continuation.caller == caller) return &slot;
    }
    return nullptr;
}

os::core::Result<void> IpcContinuationTable::arm(
    ThreadId caller,
    AddressSpaceEpoch epoch,
    std::uint64_t exchange_address,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (caller == invalid_thread) {
        return continuation_error(ipc_continuation_errors::invalid_thread);
    }
    if (!epoch.valid() || !epochs.active(epoch)) {
        return continuation_error(ipc_continuation_errors::invalid_epoch);
    }
    if (exchange_address == 0ULL) {
        return continuation_error(ipc_continuation_errors::invalid_exchange);
    }
    if (slot_for(caller) != nullptr) {
        return continuation_error(ipc_continuation_errors::already_armed);
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot.occupied = true;
        slot.continuation = IpcSendContinuation{
            .caller = caller,
            .epoch = epoch,
            .exchange_address = exchange_address,
        };
        ++occupied_;
        return {};
    }
    return continuation_error(ipc_continuation_errors::exhausted);
}

os::core::Result<IpcSendContinuation> IpcContinuationTable::take(
    ThreadId caller,
    AddressSpaceEpoch expected,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto* slot = slot_for(caller);
    if (slot == nullptr) {
        return continuation_error(ipc_continuation_errors::not_armed);
    }

    const IpcSendContinuation continuation = slot->continuation;
    *slot = Slot{};
    --occupied_;

    if (!expected.valid() || !epochs.active(expected) || !(continuation.epoch == expected)) {
        return continuation_error(ipc_continuation_errors::stale);
    }
    return continuation;
}

os::core::Result<void> IpcContinuationTable::cancel(ThreadId caller) noexcept {
    auto* slot = slot_for(caller);
    if (slot == nullptr) {
        return continuation_error(ipc_continuation_errors::not_armed);
    }
    *slot = Slot{};
    --occupied_;
    return {};
}

void IpcContinuationTable::release_thread(ThreadId caller) noexcept {
    auto* slot = slot_for(caller);
    if (slot == nullptr) return;
    *slot = Slot{};
    --occupied_;
}

} // namespace os::kernel
