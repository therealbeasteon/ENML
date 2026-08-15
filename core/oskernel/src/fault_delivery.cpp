#include <os/kernel/fault_delivery.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error delivery_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

} // namespace

FaultDeliveryTable::Slot* FaultDeliveryTable::find(ThreadId pager) noexcept {
    for (auto& slot : slots_) {
        if (slot.state != State::free && slot.pager == pager) return &slot;
    }
    return nullptr;
}

const FaultDeliveryTable::Slot* FaultDeliveryTable::find(ThreadId pager) const noexcept {
    for (const auto& slot : slots_) {
        if (slot.state != State::free && slot.pager == pager) return &slot;
    }
    return nullptr;
}

bool FaultDeliveryTable::armed(ThreadId pager) const noexcept {
    if (pager == invalid_thread) return false;
    return find(pager) != nullptr;
}

os::core::Result<void> FaultDeliveryTable::arm(
    ThreadId pager,
    ThreadId faulting,
    FaultReport report) noexcept {
    if (pager == invalid_thread || faulting == invalid_thread) {
        return delivery_error(fault_delivery_errors::invalid_thread);
    }
    // A pager cannot be its own faulting thread. It would be asked a question
    // it is blocked from answering, which is a deadlock the table can refuse
    // rather than a state the scheduler has to notice.
    if (pager == faulting) {
        return delivery_error(fault_delivery_errors::invalid_thread);
    }
    // Only a report the region table said was deliverable. A terminate
    // disposition means no pager may be asked at all - that is what `sealed`
    // regions and undeclared memory produce - so arming one would route around
    // the disclosure decision instead of enforcing it.
    if (!report.deliverable()) {
        return delivery_error(fault_delivery_errors::not_armed);
    }
    // Refuse rather than overwrite, the same discipline InterruptDeliveryTable
    // and IpcContinuationTable apply. Overwriting would strand the first
    // faulting thread waiting on an answer nobody now owes it.
    if (find(pager) != nullptr) {
        return delivery_error(fault_delivery_errors::already_armed);
    }

    for (auto& slot : slots_) {
        if (slot.state != State::free) continue;
        slot.pager = pager;
        slot.delivery = FaultDelivery{report.region, faulting, report.write};
        slot.state = State::pending;
        ++occupied_;
        return {};
    }
    return delivery_error(fault_delivery_errors::exhausted);
}

os::core::Result<FaultDelivery> FaultDeliveryTable::take(ThreadId pager) noexcept {
    if (pager == invalid_thread) {
        return os::core::Result<FaultDelivery>{
            delivery_error(fault_delivery_errors::invalid_thread)};
    }
    auto* slot = find(pager);
    if (slot == nullptr || slot->state != State::pending) {
        return os::core::Result<FaultDelivery>{
            delivery_error(fault_delivery_errors::not_armed)};
    }
    // Collected, not finished. The slot stays occupied because the faulting
    // thread still has to be found when the answer arrives.
    slot->state = State::delivered;
    return slot->delivery;
}

os::core::Result<FaultDelivery> FaultDeliveryTable::answer(ThreadId pager) noexcept {
    if (pager == invalid_thread) {
        return os::core::Result<FaultDelivery>{
            delivery_error(fault_delivery_errors::invalid_thread)};
    }
    auto* slot = find(pager);
    if (slot == nullptr) {
        return os::core::Result<FaultDelivery>{
            delivery_error(fault_delivery_errors::not_armed)};
    }
    if (slot->state != State::delivered) {
        return os::core::Result<FaultDelivery>{
            delivery_error(fault_delivery_errors::not_delivered)};
    }
    const auto delivery = slot->delivery;
    *slot = Slot{};
    --occupied_;
    return delivery;
}

FaultDelivery FaultDeliveryTable::release(ThreadId pager) noexcept {
    if (pager == invalid_thread) return {};
    auto* slot = find(pager);
    if (slot == nullptr) return {};
    const auto delivery = slot->delivery;
    *slot = Slot{};
    --occupied_;
    // Returned in both states on purpose. A pager that died before collecting
    // owes the same debt as one that died after: either way a thread is
    // waiting on an answer that is never coming, and the caller has to be told
    // which one so it can be terminated rather than left blocked forever.
    return delivery;
}

} // namespace os::kernel
