#include <os/supervisor/service_broker.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include <os/core/error.hpp>

namespace os::supervisor {
namespace {

[[nodiscard]] constexpr os::core::Error broker_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] constexpr os::core::Error service_not_running() noexcept {
    return os::core::make_error(
        os::core::ErrorDomain::service,
        os::core::errors::service::not_running);
}

} // namespace

os::core::Result<BrokeredServiceEndpoint>
ServiceBroker::connect_current(
    os::core::ProcessId process_id,
    os::core::ServiceId service_id) noexcept {
    auto* process = find_process(process_id);
    if (process == nullptr) return broker_error(broker_errors::process_not_attached);

    auto authoritative = authority_->lookup(process_id);
    if (!authoritative) return authoritative.error();
    if (authoritative.value().peer != process->record.peer ||
        authoritative.value().kernel != process->record.kernel) {
        return security_error(os::core::errors::security::credential_mismatch);
    }

    auto* service = find_service(service_id);
    if (service == nullptr) return broker_error(broker_errors::service_not_registered);
    const auto index = service_index(service);
    if (index >= max_broker_services || !process->published[index]) {
        return broker_error(broker_errors::service_not_attached);
    }
    if (service->supervisor == nullptr) {
        return broker_error(broker_errors::service_not_registered);
    }

    const auto before = service->supervisor->status();
    if (before.state != ServiceState::running || before.generation == 0U) {
        return service_not_running();
    }

    auto connected = service->supervisor->connect();
    if (!connected) return connected.error();

    // Status and endpoint are owned by the same Supervisor. If lifecycle state
    // changed while the endpoint was duplicated, do not attach a stale or
    // ambiguously-labelled capability to a generation. The caller can retry
    // after the supervisor event loop converges on the new generation.
    const auto after = service->supervisor->status();
    if (after.state != ServiceState::running || after.generation == 0U ||
        after.generation != before.generation) {
        auto channel = std::move(connected).value();
        channel.close();
        return service_not_running();
    }

    BrokeredServiceEndpoint endpoint{};
    endpoint.service = service_id;
    endpoint.generation = after.generation;
    endpoint.channel = std::move(connected).value();
    if (!endpoint.valid()) return service_not_running();
    return endpoint;
}

} // namespace os::supervisor
