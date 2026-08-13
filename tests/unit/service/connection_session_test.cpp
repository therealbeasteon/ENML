#include <os/supervisor/connection_session.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::supervisor;

    ConnectionAdmissionAuthority admission{};
    SubsystemLeaseAuthority usb_leases{SubsystemDomain::usb_data};
    SubsystemLeaseAuthority bluetooth_leases{SubsystemDomain::bluetooth};
    constexpr os::core::PrincipalId principal{42U};
    constexpr std::uint64_t lock_generation = 11ULL;
    const UserPresenceProof proof{
        .authentication_generation = 8ULL,
        .lock_generation = lock_generation,
        .password_or_pin_verified = true,
        .duress_authentication = false,
    };

    auto usb = admit_connection_session(
        admission,
        usb_leases,
        principal,
        ConnectionTransport::usb,
        ConnectionPurpose::file_transfer,
        proof,
        lock_generation);
    require(usb.has_value());
    require(usb->valid());
    require(admission.active_grants() == 1U);
    require(usb_leases.counts().total() == 1U);

    // A transport cannot be coupled to the wrong attack-surface domain.
    auto wrong_domain = admit_connection_session(
        admission,
        bluetooth_leases,
        principal,
        ConnectionTransport::usb,
        ConnectionPurpose::file_transfer,
        proof,
        lock_generation);
    require(!wrong_domain.has_value());
    require(admission.active_grants() == 1U);

    require(close_connection_session(admission, usb_leases, usb.value()));
    require(admission.active_grants() == 0U);
    require(usb_leases.counts().total() == 0U);

    // Exhaust the USB lease table. A newly minted data grant must be rolled
    // back if the corresponding hardware lease cannot be acquired.
    for (std::size_t i = 0U; i < SubsystemLeaseAuthority::max_leases; ++i) {
        auto lease = usb_leases.acquire(principal, LeaseKind::interactive);
        require(lease.has_value());
    }
    require(usb_leases.counts().total() == SubsystemLeaseAuthority::max_leases);

    auto exhausted = admit_connection_session(
        admission,
        usb_leases,
        principal,
        ConnectionTransport::usb,
        ConnectionPurpose::debug,
        proof,
        lock_generation);
    require(!exhausted.has_value());
    require(admission.active_grants() == 0U);

    // Charging intentionally creates neither a data session nor a hardware-data lease.
    auto charge_session = admit_connection_session(
        admission,
        usb_leases,
        principal,
        ConnectionTransport::usb,
        ConnectionPurpose::charge_only,
        UserPresenceProof{},
        lock_generation);
    require(!charge_session.has_value());

    return 0;
}
