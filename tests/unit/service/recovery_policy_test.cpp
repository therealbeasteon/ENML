#include <cstdlib>

#include <os/supervisor/recovery_policy.hpp>

namespace {

void require(bool value) {
    if (!value) std::abort();
}

} // namespace

int main() {
    using namespace os::supervisor;

    RecoveryContext ready{.owner_authenticated = true};
    require(may_user_restart(RecoveryDomain::network, ready).allowed);
    require(may_user_restart(RecoveryDomain::bluetooth, ready).allowed);
    require(may_user_restart(RecoveryDomain::audio, ready).allowed);
    require(may_user_restart(RecoveryDomain::display, ready).allowed);
    require(may_user_restart(RecoveryDomain::shell, ready).allowed);
    require(may_user_restart(RecoveryDomain::storage, ready).allowed);
    require(may_user_restart(RecoveryDomain::telephony, ready).allowed);

    auto unauthenticated = ready;
    unauthenticated.owner_authenticated = false;
    require(!may_user_restart(RecoveryDomain::network, unauthenticated).allowed);

    auto destroying = ready;
    destroying.destruction_pending = true;
    require(!may_user_restart(RecoveryDomain::display, destroying).allowed);

    auto storage_busy = ready;
    storage_busy.protected_storage_transaction_pending = true;
    auto storage_decision = may_user_restart(RecoveryDomain::storage, storage_busy);
    require(!storage_decision.allowed);
    require(storage_decision.reason == RecoveryBlockReason::protected_storage_transaction_pending);
    // Unrelated domains remain locally recoverable while Storage commits.
    require(may_user_restart(RecoveryDomain::audio, storage_busy).allowed);

    auto emergency = ready;
    emergency.emergency_operation_active = true;
    require(!may_user_restart(RecoveryDomain::telephony, emergency).allowed);
    require(may_user_restart(RecoveryDomain::display, emergency).allowed);

    auto updating = ready;
    updating.update_in_progress = true;
    require(!may_user_restart(RecoveryDomain::network, updating).allowed);

    require(!may_user_restart(RecoveryDomain::key_service, ready).allowed);
    require(!may_user_restart(RecoveryDomain::kernel, ready).allowed);
    require(recovery_disposition(RecoveryDomain::kernel) == RecoveryDisposition::full_reboot_only);

    require(may_user_restart(RecoveryDomain::display, ready).restart_dependents);
    require(!may_user_restart(RecoveryDomain::network, ready).restart_dependents);

    return EXIT_SUCCESS;
}
