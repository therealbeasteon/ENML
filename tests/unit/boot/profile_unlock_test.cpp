#include <cassert>

#include <os/boot/profile_unlock.hpp>

namespace {

os::boot::MeasurementDigest digest(std::uint8_t seed) {
    os::boot::MeasurementDigest out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::uint8_t>(seed + i);
    return out;
}

} // namespace

int main() {
    const auto trusted = digest(1U);
    const auto altered = digest(2U);

    os::boot::SealingPolicy sealing{os::boot::MeasurementSupport::measured};
    assert(sealing.seal(trusted));
    os::boot::ProfileUnlockPolicy policy{sealing};

    os::boot::ProfileUnlockEvidence evidence{
        .user = os::core::UserId{42U},
        .boot_measurement = trusted,
        .gate_assurance = os::boot::CredentialGateAssurance::hardware_rate_limited_rollback_resistant,
        .credential_accepted = true,
        .destruction_pending = false,
    };
    assert(policy.may_release_profile_key(evidence));

    auto wrong_boot = evidence;
    wrong_boot.boot_measurement = altered;
    assert(!policy.may_release_profile_key(wrong_boot));

    auto software_gate = evidence;
    software_gate.gate_assurance = os::boot::CredentialGateAssurance::software_only;
    assert(!policy.may_release_profile_key(software_gate));

    auto resettable_gate = evidence;
    resettable_gate.gate_assurance = os::boot::CredentialGateAssurance::hardware_rate_limited;
    assert(!policy.may_release_profile_key(resettable_gate));

    auto wrong_credential = evidence;
    wrong_credential.credential_accepted = false;
    assert(!policy.may_release_profile_key(wrong_credential));

    auto pending_destroy = evidence;
    pending_destroy.destruction_pending = true;
    assert(!policy.may_release_profile_key(pending_destroy));

    auto no_user = evidence;
    no_user.user = os::core::UserId{};
    assert(!policy.may_release_profile_key(no_user));

    os::boot::SealingPolicy unmeasured{os::boot::MeasurementSupport::unmeasured};
    os::boot::ProfileUnlockPolicy unmeasured_policy{unmeasured};
    assert(!unmeasured_policy.may_release_profile_key(evidence));

    return 0;
}
