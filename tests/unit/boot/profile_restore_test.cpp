#include <os/boot/profile_restore.hpp>

#include <cstdlib>

namespace {

using namespace os::boot;

MeasurementDigest measurement(std::uint8_t seed) {
    MeasurementDigest digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) digest[i] = static_cast<std::uint8_t>(seed + i);
    return digest;
}

void require(bool value) { if (!value) std::abort(); }

} // namespace

int main() {
    const auto digest = measurement(7U);
    SealingPolicy sealing{MeasurementSupport::measured};
    require(static_cast<bool>(sealing.seal(digest)));
    ProfileUnlockPolicy unlock{sealing};
    ProfileRestorePolicy restore{unlock};

    ProfileProtectorHeaderV1 record{
        .user = os::core::UserId{42U},
        .security_epoch = SecurityEpochValue{9U},
        .boot_measurement = digest,
        .gate_slot = CredentialGateSlotId{3U},
        .generation = ProtectorGeneration{5U},
        .provider_blob_size = 32U,
    };
    ProfileRestoreEvidence evidence{
        .unlock = ProfileUnlockEvidence{
            .user = os::core::UserId{42U},
            .boot_measurement = digest,
            .gate_assurance = CredentialGateAssurance::hardware_rate_limited_rollback_resistant,
            .credential_accepted = true,
            .destruction_pending = false,
        },
        .current_security_epoch = SecurityEpochValue{9U},
        .current_gate_slot = CredentialGateSlotId{3U},
        .minimum_generation = ProtectorGeneration{5U},
    };

    require(static_cast<bool>(restore.may_restore(record, evidence)));

    auto changed = evidence;
    changed.current_security_epoch = SecurityEpochValue{10U};
    require(!restore.may_restore(record, changed));

    changed = evidence;
    changed.unlock.boot_measurement = measurement(8U);
    require(!restore.may_restore(record, changed));

    changed = evidence;
    changed.current_gate_slot = CredentialGateSlotId{4U};
    require(!restore.may_restore(record, changed));

    changed = evidence;
    changed.minimum_generation = ProtectorGeneration{6U};
    require(!restore.may_restore(record, changed));

    changed = evidence;
    changed.unlock.credential_accepted = false;
    require(!restore.may_restore(record, changed));

    changed = evidence;
    changed.unlock.destruction_pending = true;
    require(!restore.may_restore(record, changed));

    auto other_record = record;
    other_record.user = os::core::UserId{43U};
    require(!restore.may_restore(other_record, evidence));

    // A record claiming an epoch ahead of hardware is also invalid. Disk state
    // cannot authorize hardware monotonic state to move forward implicitly.
    other_record = record;
    other_record.security_epoch = SecurityEpochValue{10U};
    require(!restore.may_restore(other_record, evidence));

    return 0;
}
