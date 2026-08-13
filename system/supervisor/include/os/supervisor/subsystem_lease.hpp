#pragma once

#include <cstdint>

namespace os::supervisor {

enum class SubsystemDomain : std::uint8_t {
    network = 1U,
    bluetooth = 2U,
    camera = 3U,
    microphone = 4U,
    location = 5U,
    sensors = 6U,
    usb_data = 7U,
    display = 8U,
    audio = 9U,
    telephony = 10U,
    storage = 11U,
    key_service = 12U,
};

enum class IdleDisposition : std::uint8_t {
    stop_and_power_gate = 1U,
    stop_service_keep_hardware_safe = 2U,
    quiesce_only = 3U,
    always_resident = 4U,
};

enum class SubsystemState : std::uint8_t {
    off = 1U,
    starting = 2U,
    active = 3U,
    quiescing = 4U,
    revoking = 5U,
    stopping = 6U,
    faulted = 7U,
};

struct LeaseCount final {
    std::uint16_t interactive {0U};
    std::uint16_t background {0U};
    std::uint16_t system {0U};

    [[nodiscard]] constexpr std::uint32_t total() const noexcept {
        return static_cast<std::uint32_t>(interactive) +
               static_cast<std::uint32_t>(background) +
               static_cast<std::uint32_t>(system);
    }
};

struct QuiesceContext final {
    bool security_transaction_pending {false};
    bool protected_storage_commit_pending {false};
    bool emergency_operation_active {false};
    bool update_in_progress {false};
    bool wake_source_required {false};
};

struct QuiescePlan final {
    bool may_quiesce {false};
    bool revoke_client_capabilities {false};
    bool revoke_dma_and_irqs {false};
    bool zero_ephemeral_secrets {false};
    bool stop_service {false};
    bool power_gate_hardware {false};
};

[[nodiscard]] constexpr IdleDisposition
idle_disposition(SubsystemDomain domain) noexcept {
    switch (domain) {
    case SubsystemDomain::network:
    case SubsystemDomain::bluetooth:
    case SubsystemDomain::camera:
    case SubsystemDomain::microphone:
    case SubsystemDomain::location:
    case SubsystemDomain::sensors:
    case SubsystemDomain::usb_data:
        return IdleDisposition::stop_and_power_gate;
    case SubsystemDomain::display:
    case SubsystemDomain::audio:
        return IdleDisposition::stop_service_keep_hardware_safe;
    case SubsystemDomain::telephony:
        return IdleDisposition::quiesce_only;
    case SubsystemDomain::storage:
    case SubsystemDomain::key_service:
        return IdleDisposition::always_resident;
    }
    return IdleDisposition::always_resident;
}

// Security ordering for shutting down optional attack surface:
//   stop new work -> quiesce -> revoke client authority -> revoke device DMA/IRQ
//   -> zero transient secrets -> stop service/driver -> power gate hardware.
// A backend may implement fewer physical power states, but may never reorder
// revocation after hardware/service shutdown because stale capabilities could
// otherwise survive a restart generation.
[[nodiscard]] constexpr QuiescePlan
plan_idle_transition(
    SubsystemDomain domain,
    LeaseCount leases,
    const QuiesceContext& context) noexcept {
    if (leases.total() != 0U) {
        return {};
    }

    if (context.security_transaction_pending || context.update_in_progress) {
        return {};
    }
    if (domain == SubsystemDomain::storage && context.protected_storage_commit_pending) {
        return {};
    }
    if (domain == SubsystemDomain::telephony && context.emergency_operation_active) {
        return {};
    }

    const auto disposition = idle_disposition(domain);
    if (disposition == IdleDisposition::always_resident) {
        return {};
    }

    QuiescePlan plan{
        .may_quiesce = true,
        .revoke_client_capabilities = true,
        .revoke_dma_and_irqs = false,
        .zero_ephemeral_secrets = true,
        .stop_service = false,
        .power_gate_hardware = false,
    };

    if (disposition == IdleDisposition::quiesce_only) {
        return plan;
    }

    plan.stop_service = true;
    if (disposition == IdleDisposition::stop_and_power_gate) {
        plan.revoke_dma_and_irqs = true;
        plan.power_gate_hardware = !context.wake_source_required;
    }
    return plan;
}

} // namespace os::supervisor
