#include <os/app/manager.hpp>

#include <cstdint>

#include <os/core/error.hpp>

namespace os::app {
namespace {

[[nodiscard]] constexpr os::core::Error manager_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

} // namespace

os::core::Result<void> ApplicationManager::deliver_input_event(
    const ApplicationInputEventV1& event) noexcept {
    if (!event.valid()) return manager_error(os::core::errors::service::invalid_request);

    InstanceSlot* target = nullptr;
    for (auto& slot : instances_) {
        if (slot.occupied && slot.info.valid() && slot.info.identity == event.target) {
            target = &slot;
            break;
        }
    }
    if (target == nullptr) return manager_error(manager_errors::input_target_not_found);
    if (!target->input_event_sender.valid()) {
        return manager_error(manager_errors::input_endpoint_unavailable);
    }
    if (event.sequence <= target->last_input_event_sequence) {
        return manager_error(manager_errors::input_event_replay);
    }

    auto sent = send_application_input_event(target->input_event_sender, event);
    if (!sent) {
        // A dead application-side event endpoint is not silently replaced.
        // The application must reacquire through its authenticated runtime
        // session; until then delivery fails closed rather than buffering an
        // unbounded hidden event queue.
        target->input_event_sender.close();
        return manager_error(manager_errors::input_endpoint_unavailable);
    }

    target->last_input_event_sequence = event.sequence;
    return {};
}

} // namespace os::app
