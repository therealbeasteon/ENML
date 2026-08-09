#include <os/supervisor/service_broker.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>

namespace os::supervisor {
namespace {

[[nodiscard]] constexpr os::core::Error broker_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] constexpr bool unknown_process(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::security &&
        error.code == os::core::errors::security::unknown_process;
}

} // namespace

ServiceBroker::~ServiceBroker() {
    for (auto& process : processes_) {
        if (!process.occupied) continue;
        const auto id = process.record.peer.process;
        os::core::discard_result(detach_process(id));
    }
}

ServiceBroker::ServiceSlot*
ServiceBroker::find_service(os::core::ServiceId service) noexcept {
    for (auto& slot : services_) {
        if (slot.occupied && slot.id == service) return &slot;
    }
    return nullptr;
}

const ServiceBroker::ServiceSlot*
ServiceBroker::find_service(os::core::ServiceId service) const noexcept {
    for (const auto& slot : services_) {
        if (slot.occupied && slot.id == service) return &slot;
    }
    return nullptr;
}

std::size_t ServiceBroker::service_index(const ServiceSlot* slot) const noexcept {
    if (slot == nullptr) return max_broker_services;
    const auto* begin = services_.data();
    const auto* end = begin + services_.size();
    if (slot < begin || slot >= end) return max_broker_services;
    return static_cast<std::size_t>(slot - begin);
}

ServiceBroker::ProcessSlot*
ServiceBroker::find_process(os::core::ProcessId process) noexcept {
    for (auto& slot : processes_) {
        if (slot.occupied && slot.record.peer.process == process) return &slot;
    }
    return nullptr;
}

const ServiceBroker::ProcessSlot*
ServiceBroker::find_process(os::core::ProcessId process) const noexcept {
    for (const auto& slot : processes_) {
        if (slot.occupied && slot.record.peer.process == process) return &slot;
    }
    return nullptr;
}

ServiceBroker::ProcessSlot* ServiceBroker::find_native_pid(pid_t native_pid) noexcept {
    for (auto& slot : processes_) {
        if (slot.occupied &&
            slot.record.kernel.process_id == static_cast<std::int64_t>(native_pid)) {
            return &slot;
        }
    }
    return nullptr;
}

os::core::Result<void>
ServiceBroker::register_service(
    os::core::ServiceId service,
    Supervisor& supervisor) noexcept {
    if (authority_ == nullptr || service.value() == 0U ||
        supervisor.service_id() != service ||
        &supervisor.process_authority() != authority_) {
        return broker_error(broker_errors::invalid_request);
    }
    if (find_service(service) != nullptr) {
        return broker_error(broker_errors::service_conflict);
    }
    for (const auto& slot : services_) {
        if (slot.occupied && slot.supervisor == &supervisor) {
            return broker_error(broker_errors::service_conflict);
        }
    }
    for (auto& slot : services_) {
        if (!slot.occupied) {
            slot = ServiceSlot{
                .occupied = true,
                .id = service,
                .supervisor = &supervisor,
            };
            return {};
        }
    }
    return broker_error(broker_errors::service_capacity);
}

os::core::Result<os::service::ProcessIdentityRecord>
ServiceBroker::attach_process(
    pid_t native_pid,
    os::core::PrincipalId principal,
    os::core::UserId user,
    std::span<const os::core::ServiceId> requested_services) noexcept {
    if (authority_ == nullptr || native_pid <= 0 ||
        !os::core::valid_principal(principal) || requested_services.empty() ||
        requested_services.size() > max_broker_services) {
        return broker_error(broker_errors::invalid_request);
    }

    std::array<std::size_t, max_broker_services> requested_indices{};
    for (std::size_t index = 0U; index < requested_services.size(); ++index) {
        if (requested_services[index].value() == 0U) {
            return broker_error(broker_errors::invalid_request);
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (requested_services[prior] == requested_services[index]) {
                return broker_error(broker_errors::invalid_request);
            }
        }
        auto* service = find_service(requested_services[index]);
        if (service == nullptr) {
            return broker_error(broker_errors::service_not_registered);
        }
        const auto service_slot = service_index(service);
        if (service_slot >= max_broker_services) {
            return broker_error(broker_errors::invalid_request);
        }
        requested_indices[index] = service_slot;
    }

    ProcessSlot* process = find_native_pid(native_pid);
    bool created_process = false;
    if (process != nullptr) {
        auto live = authority_->lookup(process->record.peer.process);
        if (!live) {
            const auto stale_id = process->record.peer.process;
            auto detached = detach_process(stale_id);
            if (!detached && !unknown_process(detached.error())) return detached.error();
            process = nullptr;
        } else if (process->record.peer.principal != principal ||
                   process->record.peer.user != user) {
            return security_error(os::core::errors::security::credential_mismatch);
        }
    }

    if (process == nullptr) {
        for (auto& candidate : processes_) {
            if (!candidate.occupied) {
                process = &candidate;
                break;
            }
        }
        if (process == nullptr) return broker_error(broker_errors::process_capacity);

        auto authoritative = authority_->acquire(native_pid, principal, user);
        if (!authoritative) return authoritative.error();
        *process = ProcessSlot{
            .occupied = true,
            .record = authoritative.value(),
            .published = {},
        };
        created_process = true;
    }

    std::array<bool, max_broker_services> newly_published{};
    os::core::Error publish_failure{};
    bool failed = false;

    for (std::size_t request = 0U; request < requested_services.size(); ++request) {
        const auto service_slot = requested_indices[request];
        if (process->published[service_slot]) continue;

        auto& service = services_[service_slot];
        if (!service.occupied || service.supervisor == nullptr) {
            publish_failure = broker_error(broker_errors::service_not_registered);
            failed = true;
            break;
        }
        auto published = service.supervisor->register_process(native_pid, principal, user);
        if (!published) {
            publish_failure = published.error();
            failed = true;
            break;
        }

        process->published[service_slot] = true;
        newly_published[service_slot] = true;
        if (published.value().peer != process->record.peer ||
            published.value().kernel != process->record.kernel) {
            publish_failure = security_error(os::core::errors::security::credential_mismatch);
            failed = true;
            break;
        }
    }

    if (!failed) return process->record;

    // Restore the pre-call publication invariant in reverse service order. A
    // rollback failure is surfaced and the broker deliberately keeps its base
    // authority reference plus any publication that could not be removed; it
    // must never make that ProcessId available for an unrelated execution.
    os::core::Error rollback_failure{};
    bool rollback_failed = false;
    for (std::size_t reverse = requested_services.size(); reverse > 0U; --reverse) {
        const auto service_slot = requested_indices[reverse - 1U];
        if (!newly_published[service_slot]) continue;
        auto& service = services_[service_slot];
        auto revoked = service.supervisor->unregister_process(process->record.peer.process);
        if (!revoked && !unknown_process(revoked.error())) {
            if (!rollback_failed) rollback_failure = revoked.error();
            rollback_failed = true;
            continue;
        }
        process->published[service_slot] = false;
    }

    bool any_published = false;
    for (bool published : process->published) {
        if (published) {
            any_published = true;
            break;
        }
    }
    if (created_process && !any_published) {
        auto released = authority_->release(process->record.peer.process);
        if (!released && !unknown_process(released.error()) && !rollback_failed) {
            rollback_failure = released.error();
            rollback_failed = true;
        }
        if (!rollback_failed) *process = ProcessSlot{};
    }

    return rollback_failed ? rollback_failure : publish_failure;
}

os::core::Result<os::ipc::Channel>
ServiceBroker::connect(
    os::core::ProcessId process_id,
    os::core::ServiceId service_id) noexcept {
    auto* process = find_process(process_id);
    if (process == nullptr) return broker_error(broker_errors::process_not_attached);

    auto authoritative = authority_->lookup(process_id);
    if (!authoritative) return authoritative.error();
    if (authoritative.value().peer != process->record.peer) {
        return security_error(os::core::errors::security::credential_mismatch);
    }

    auto* service = find_service(service_id);
    if (service == nullptr) return broker_error(broker_errors::service_not_registered);
    const auto index = service_index(service);
    if (index >= max_broker_services || !process->published[index]) {
        return broker_error(broker_errors::service_not_attached);
    }
    return service->supervisor->connect();
}

os::core::Result<void>
ServiceBroker::detach_process(os::core::ProcessId process_id) noexcept {
    auto* process = find_process(process_id);
    if (process == nullptr) return broker_error(broker_errors::process_not_attached);

    os::core::Error first_failure{};
    bool failed = false;
    for (std::size_t index = 0U; index < services_.size(); ++index) {
        if (!process->published[index]) continue;
        auto& service = services_[index];
        if (!service.occupied || service.supervisor == nullptr) {
            if (!failed) first_failure = broker_error(broker_errors::service_not_registered);
            failed = true;
            continue;
        }
        auto revoked = service.supervisor->unregister_process(process_id);
        if (!revoked && !unknown_process(revoked.error())) {
            if (!failed) first_failure = revoked.error();
            failed = true;
            continue;
        }
        process->published[index] = false;
    }
    if (failed) return first_failure;

    auto released = authority_->release(process_id);
    if (!released && !unknown_process(released.error())) return released.error();
    *process = ProcessSlot{};
    return {};
}

os::core::Result<os::service::ProcessIdentityRecord>
ServiceBroker::lookup(os::core::ProcessId process_id) const noexcept {
    const auto* process = find_process(process_id);
    if (process == nullptr) return broker_error(broker_errors::process_not_attached);
    auto authoritative = authority_->lookup(process_id);
    if (!authoritative) return authoritative.error();
    if (authoritative.value().peer != process->record.peer ||
        authoritative.value().kernel != process->record.kernel) {
        return security_error(os::core::errors::security::credential_mismatch);
    }
    return authoritative.value();
}

std::size_t ServiceBroker::service_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& service : services_) {
        if (service.occupied) ++count;
    }
    return count;
}

std::size_t ServiceBroker::process_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& process : processes_) {
        if (process.occupied) ++count;
    }
    return count;
}

} // namespace os::supervisor
