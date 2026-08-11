#pragma once

#include <cstdint>

#include <os/boot/sealing.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>

namespace os::boot {

// A profile key is not released merely because a credential matched. Release
// requires a trusted measured boot and a hardware-backed credential gate with
// durable anti-hammering state. This deliberately mirrors the security
// properties of TPM+PIN / secure-element-backed mobile storage without importing
// a vendor ABI.
enum class CredentialGateAssurance : std::uint8_t {
    software_only = 1U,
    hardware_rate_limited = 2U,
    hardware_rate_limited_rollback_resistant = 3U,
};

[[nodiscard]] constexpr bool valid_credential_gate_assurance(
    CredentialGateAssurance value) noexcept {
    switch (value) {
    case CredentialGateAssurance::software_only:
    case CredentialGateAssurance::hardware_rate_limited:
    case CredentialGateAssurance::hardware_rate_limited_rollback_resistant:
        return true;
    }
    return false;
}

struct ProfileUnlockEvidence final {
    os::core::UserId user {};
    MeasurementDigest boot_measurement {};
    CredentialGateAssurance gate_assurance {CredentialGateAssurance::software_only};
    bool credential_accepted {false};
    bool destruction_pending {false};
};

namespace profile_unlock_errors {
inline constexpr std::uint32_t invalid_user = 1U;
inline constexpr std::uint32_t untrusted_boot = 2U;
inline constexpr std::uint32_t credential_refused = 3U;
inline constexpr std::uint32_t insufficient_anti_hammering = 4U;
inline constexpr std::uint32_t destruction_pending = 5U;
} // namespace profile_unlock_errors

[[nodiscard]] constexpr os::core::Error profile_unlock_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::boot, 0x500U + code);
}

// Policy gate for releasing a credential-encrypted profile key after boot.
//
// Production protected profiles require rollback-resistant hardware anti-
// hammering. A software-only password KDF can still exist for development and
// recovery tooling, but may not satisfy this release policy. This prevents
// reboot/offline rollback from resetting the brute-force budget.
class ProfileUnlockPolicy final {
public:
    explicit ProfileUnlockPolicy(const SealingPolicy& sealing) noexcept : sealing_(&sealing) {}

    [[nodiscard]] os::core::Result<void>
    may_release_profile_key(const ProfileUnlockEvidence& evidence) const noexcept {
        if (evidence.user.value() == 0U) {
            return profile_unlock_error(profile_unlock_errors::invalid_user);
        }
        if (evidence.destruction_pending) {
            return profile_unlock_error(profile_unlock_errors::destruction_pending);
        }
        if (!evidence.credential_accepted) {
            return profile_unlock_error(profile_unlock_errors::credential_refused);
        }
        if (!valid_credential_gate_assurance(evidence.gate_assurance) ||
            evidence.gate_assurance !=
                CredentialGateAssurance::hardware_rate_limited_rollback_resistant) {
            return profile_unlock_error(profile_unlock_errors::insufficient_anti_hammering);
        }
        if (sealing_ == nullptr || !sealing_->sealing_available()) {
            return profile_unlock_error(profile_unlock_errors::untrusted_boot);
        }
        auto release = sealing_->may_release(evidence.boot_measurement);
        if (!release) {
            return profile_unlock_error(profile_unlock_errors::untrusted_boot);
        }
        return {};
    }

private:
    const SealingPolicy* sealing_ {nullptr};
};

} // namespace os::boot
