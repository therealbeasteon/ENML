#include <os/supervisor/connection_admission.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::supervisor;

    ConnectionAdmissionAuthority authority{};
    constexpr std::uint64_t lock_generation = 9ULL;

    // Power is always separable from data authority.
    const UserPresenceProof no_auth{};
    auto charge = authority.admit(
        ConnectionTransport::usb,
        ConnectionPurpose::charge_only,
        no_auth,
        lock_generation);
    require(charge.valid());
    require(!charge.data_signaling);
    require(grant_authorizes(
        charge, ConnectionTransport::usb, ConnectionPurpose::charge_only,
        lock_generation));
    require(!grant_authorizes(
        charge, ConnectionTransport::usb, ConnectionPurpose::file_transfer,
        lock_generation));

    // A locked/unauthenticated device cannot promote the same physical cable to
    // file transfer, debug, tethering, audio/display data, or accessory control.
    require(!authority.admit(
        ConnectionTransport::usb,
        ConnectionPurpose::file_transfer,
        no_auth,
        lock_generation).valid());
    require(!authority.admit(
        ConnectionTransport::debug_link,
        ConnectionPurpose::debug,
        no_auth,
        lock_generation).valid());

    const UserPresenceProof proof{
        .authentication_generation = 5ULL,
        .lock_generation = lock_generation,
        .password_or_pin_verified = true,
        .duress_authentication = false,
    };
    auto usb_data = authority.admit(
        ConnectionTransport::usb,
        ConnectionPurpose::file_transfer,
        proof,
        lock_generation);
    require(usb_data.valid() && usb_data.data_signaling);

    auto bluetooth = authority.admit(
        ConnectionTransport::bluetooth,
        ConnectionPurpose::accessory_control,
        proof,
        lock_generation);
    require(bluetooth.valid() && bluetooth.data_signaling);

    // A proof from an earlier lock generation cannot authorize a newly locked
    // device, and duress authentication never mints external data authority.
    require(!authority.admit(
        ConnectionTransport::wifi_direct,
        ConnectionPurpose::file_transfer,
        proof,
        lock_generation + 1ULL).valid());
    auto duress = proof;
    duress.duress_authentication = true;
    require(!authority.admit(
        ConnectionTransport::usb,
        ConnectionPurpose::file_transfer,
        duress,
        lock_generation).valid());

    // Re-lock/disconnect revokes all existing data grants even if the physical
    // endpoint remains connected or a wireless bond still exists.
    authority.revoke_all();
    require(!authority.active(usb_data, lock_generation));
    require(!authority.active(bluetooth, lock_generation));

    return 0;
}
