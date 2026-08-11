#pragma once

#include <cstdint>

#include <os/boot/profile_protector.hpp>
#include <os/boot/profile_unlock.hpp>
#include <os/core/result.hpp>

namespace os::boot {

// Runtime trusted state supplied from hardware/early boot. None of these values
// are accepted from the persistent protector record itself.
struct ProfileRestoreEvidence final {
    ProfileUnlockEvidence unlock {};
    SecurityEpochValue current_security_epoch {};
    CredentialGateSlotId current_gate_slot {};
    ProtectorGeneration minimum_generation {};
};

namespace profile_restore_errors {
inline constexpr std::uint32_t invalid_evidence = 1U;
inline constexpr std::uint32_t wrong_user = 2U;
inline constexpr std::uint32_t stale_epoch = 3U;
inline constexpr std::uint32_t measurement_mismatch = 4U;
inline constexpr std::uint32_t gate_slot_mismatch = 5U;
inline constexpr std::uint32_t stale_generation = 6U;
inline constexpr std::uint32_t unlock_refused = 7U;
} // namespace profile_restore_errors

[[nodiscard]] constexpr os::core::Error profile_restore_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::boot, 0x600U + code);
}

// Final policy gate before an opaque provider blob may be presented to a
// PersistentKeyProvider for restoration. The record is untrusted disk input;
// every security-sensitive field is compared with independently trusted current
// state. Equality on the security epoch is deliberate: both rollback (record <
// hardware) and an impossible future record (record > hardware) fail closed.
class ProfileRestorePolicy final {
public:
    explicit ProfileRestorePolicy(const ProfileUnlockPolicy& unlock_policy) noexcept
        : unlock_policy_(&unlock_policy) {}

    [[nodiscard]] os::core::Result<void>
    may_restore(
        const ProfileProtectorHeaderV1& record,
        const ProfileRestoreEvidence& evidence) const noexcept {
        if (!valid_profile_protector_header(record) ||
            !evidence.current_security_epoch.valid() ||
            !evidence.current_gate_slot.valid() ||
            !evidence.minimum_generation.valid()) {
            return profile_restore_error(profile_restore_errors::invalid_evidence);
        }
        if (record.user != evidence.unlock.user) {
            return profile_restore_error(profile_restore_errors::wrong_user);
        }
        if (record.security_epoch != evidence.current_security_epoch) {
            return profile_restore_error(profile_restore_errors::stale_epoch);
        }
        if (record.boot_measurement != evidence.unlock.boot_measurement) {
            return profile_restore_error(profile_restore_errors::measurement_mismatch);
        }
        if (record.gate_slot != evidence.current_gate_slot) {
            return profile_restore_error(profile_restore_errors::gate_slot_mismatch);
        }
        if (record.generation.value < evidence.minimum_generation.value) {
            return profile_restore_error(profile_restore_errors::stale_generation);
        }
        if (unlock_policy_ == nullptr ||
            !unlock_policy_->may_release_profile_key(evidence.unlock)) {
            return profile_restore_error(profile_restore_errors::unlock_refused);
        }
        return {};
    }

private:
    const ProfileUnlockPolicy* unlock_policy_ {nullptr};
};

} // namespace os::boot
