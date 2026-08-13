#pragma once

#include <optional>

#include <os/core/identity.hpp>
#include <os/supervisor/connection_admission.hpp>
#include <os/supervisor/subsystem_lease_authority.hpp>

namespace os::supervisor {

struct ConnectionSession final {
    ConnectionGrant grant {};
    SubsystemLeaseToken lease {};
    os::core::PrincipalId principal {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return grant.valid() && grant.data_signaling && lease.valid() &&
               os::core::valid_principal(principal);
    }
};

// A data relationship is admitted transactionally with the attack-surface
// lease that keeps the required hardware/service domain alive. If the lease
// cannot be acquired, the just-minted grant is rolled back before returning.
[[nodiscard]] constexpr std::optional<ConnectionSession> admit_connection_session(
    ConnectionAdmissionAuthority& admission,
    SubsystemLeaseAuthority& leases,
    os::core::PrincipalId principal,
    ConnectionTransport transport,
    ConnectionPurpose purpose,
    const UserPresenceProof& proof,
    std::uint64_t current_lock_generation) noexcept {
    if (!os::core::valid_principal(principal) ||
        purpose == ConnectionPurpose::charge_only) return std::nullopt;

    const auto expected_domain = connection_subsystem_domain(transport);
    if (!expected_domain.has_value() || leases.domain() != expected_domain.value()) {
        return std::nullopt;
    }

    const auto grant = admission.admit(
        transport, purpose, proof, current_lock_generation);
    if (!grant.valid() || !grant.data_signaling) return std::nullopt;

    const auto lease = leases.acquire(principal, LeaseKind::interactive);
    if (!lease.has_value()) {
        (void)admission.release(grant);
        return std::nullopt;
    }

    return ConnectionSession{
        .grant = grant,
        .lease = lease.value(),
        .principal = principal,
    };
}

// Session closure retires the data grant first, then releases the hardware
// lease. That ordering ensures the endpoint loses data authority before the
// subsystem becomes eligible to quiesce or power down.
[[nodiscard]] constexpr bool close_connection_session(
    ConnectionAdmissionAuthority& admission,
    SubsystemLeaseAuthority& leases,
    ConnectionSession session) noexcept {
    if (!session.valid() || leases.domain() != session.lease.domain) return false;
    if (!admission.release(session.grant)) return false;
    return leases.release(session.principal, session.lease);
}

} // namespace os::supervisor
