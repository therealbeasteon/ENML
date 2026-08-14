#include <os/kernel/interrupt_delivery.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error delivery_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
} // namespace

InterruptDeliveryTable::Slot* InterruptDeliveryTable::find(ThreadId driver) noexcept {
    if (driver == invalid_thread) return nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.driver == driver) return &slot;
    }
    return nullptr;
}

const InterruptDeliveryTable::Slot* InterruptDeliveryTable::find(ThreadId driver) const noexcept {
    if (driver == invalid_thread) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.driver == driver) return &slot;
    }
    return nullptr;
}

bool InterruptDeliveryTable::armed(ThreadId driver) const noexcept {
    return find(driver) != nullptr;
}

os::core::Result<void> InterruptDeliveryTable::arm(
    ThreadId driver, InterruptSource source, Service service) noexcept {
    if (driver == invalid_thread || source == invalid_interrupt_source) {
        return delivery_error(interrupt_delivery_errors::invalid_thread);
    }
    if (find(driver) != nullptr) {
        return delivery_error(interrupt_delivery_errors::already_armed);
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot.occupied = true;
        slot.driver = driver;
        slot.source = source;
        slot.service = service;
        return {};
    }
    return delivery_error(interrupt_delivery_errors::exhausted);
}

os::core::Result<Service> InterruptDeliveryTable::take(ThreadId driver) noexcept {
    auto* slot = find(driver);
    if (slot == nullptr) return delivery_error(interrupt_delivery_errors::not_armed);
    const Service service = slot->service;
    *slot = Slot{};
    return service;
}

bool InterruptDeliveryTable::release(ThreadId driver) noexcept {
    auto* slot = find(driver);
    if (slot == nullptr) return false;
    *slot = Slot{};
    return true;
}

} // namespace os::kernel
