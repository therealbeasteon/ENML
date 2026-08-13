#pragma once

#include <cstdint>

namespace os::supervisor {

// User-visible recovery domains are intentionally coarser than individual
// processes. A domain may contain a driver plus one or more helper services so
// the user can ask for "Restart Wi-Fi" rather than needing implementation
// knowledge. The mapping from domain to concrete processes belongs to trusted
// Supervisor configuration, never to Settings or an application.
enum class RecoveryDomain : std::uint8_t {
    shell = 1U,
    display = 2U,
    network = 3U,
    bluetooth = 4U,
    audio = 5U,
    telephony = 6U,
    camera = 7U,
    sensors = 8U,
    storage = 9U,
    key_service = 10U,
    kernel = 11U,
};

enum class RecoveryDisposition : std::uint8_t {
    user_restartable = 1U,
    user_restartable_when_quiescent = 2U,
    system_only = 3U,
    full_reboot_only = 4U,
};

enum class RecoveryBlockReason : std::uint8_t {
    none = 0U,
    authentication_required = 1U,
    security_transaction_pending = 2U,
    protected_storage_transaction_pending = 3U,
    emergency_operation_active = 4U,
    update_in_progress = 5U,
    domain_not_user_restartable = 6U,
};

struct RecoveryContext final {
    bool owner_authenticated {false};
    bool destruction_pending {false};
    bool protected_storage_transaction_pending {false};
    bool emergency_operation_active {false};
    bool update_in_progress {false};
};

struct RecoveryDecision final {
    bool allowed {false};
    RecoveryBlockReason reason {RecoveryBlockReason::domain_not_user_restartable};
    bool restart_dependents {false};
};

[[nodiscard]] constexpr RecoveryDisposition
recovery_disposition(RecoveryDomain domain) noexcept {
    switch (domain) {
    case RecoveryDomain::shell:
    case RecoveryDomain::display:
    case RecoveryDomain::network:
    case RecoveryDomain::bluetooth:
    case RecoveryDomain::audio:
    case RecoveryDomain::camera:
    case RecoveryDomain::sensors:
        return RecoveryDisposition::user_restartable;
    case RecoveryDomain::telephony:
    case RecoveryDomain::storage:
        return RecoveryDisposition::user_restartable_when_quiescent;
    case RecoveryDomain::key_service:
        return RecoveryDisposition::system_only;
    case RecoveryDomain::kernel:
        return RecoveryDisposition::full_reboot_only;
    }
    return RecoveryDisposition::system_only;
}

// Settings asks this policy; it never issues process-management operations
// directly. The result is only authorization to request a supervised recovery
// transaction. Supervisor still owns stop ordering, capability revocation,
// generation advancement, dependency restart and readiness verification.
[[nodiscard]] constexpr RecoveryDecision
may_user_restart(RecoveryDomain domain, const RecoveryContext& context) noexcept {
    if (!context.owner_authenticated) {
        return {false, RecoveryBlockReason::authentication_required, false};
    }

    const auto disposition = recovery_disposition(domain);
    if (disposition == RecoveryDisposition::system_only ||
        disposition == RecoveryDisposition::full_reboot_only) {
        return {false, RecoveryBlockReason::domain_not_user_restartable, false};
    }

    // A committed destruction transaction outranks availability. No user-facing
    // restart may create a path that reopens or delays protected-domain erasure.
    if (context.destruction_pending) {
        return {false, RecoveryBlockReason::security_transaction_pending, false};
    }

    // During an authenticated system update, service generations and executable
    // measurements are changing under controlled policy. Arbitrary user restarts
    // would make that transaction harder to reason about and recover.
    if (context.update_in_progress) {
        return {false, RecoveryBlockReason::update_in_progress, false};
    }

    if (domain == RecoveryDomain::storage &&
        context.protected_storage_transaction_pending) {
        return {false, RecoveryBlockReason::protected_storage_transaction_pending, false};
    }

    if (domain == RecoveryDomain::telephony && context.emergency_operation_active) {
        return {false, RecoveryBlockReason::emergency_operation_active, false};
    }

    // Display and shell clients hold generation-scoped presentation authority;
    // restarting those domains requires dependent endpoint reacquisition. Other
    // domains may still have dependencies, but their trusted domain map decides
    // that at execution time.
    const bool restart_dependents =
        domain == RecoveryDomain::display || domain == RecoveryDomain::shell;
    return {true, RecoveryBlockReason::none, restart_dependents};
}

} // namespace os::supervisor
