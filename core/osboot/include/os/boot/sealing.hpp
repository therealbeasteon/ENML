#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

// Binding a key to the boot state that is allowed to use it.
//
// M5.0 measures the boot chain into `BootStateV1`. M2.7 derives keys down a
// system → profile → application hierarchy. Nothing has ever connected the two,
// which means every protection in the key service assumes the operating system
// asking for a key is the one that was measured - and that assumption is exactly
// what an attacker who replaces the OS is attacking.
//
// Full-disk encryption on desktop platforms answers this by *sealing*: the key
// is released only when the platform reports the same measurements it had when
// the key was sealed. Change the boot chain and the key does not decrypt - not
// because a check refused it, but because the wrong key comes out. The credential
// stops being sufficient on its own.
//
// That is the property worth importing, and it is a bigger one than it looks.
// Without it, every defence at the lock screen - the duress path, the erasure
// threshold, the guess counter - is defeated by an attacker who does not use the
// lock screen at all: boot a modified image, read the encrypted volume, and take
// as many offline guesses as they like. Sealing is what makes the lock screen
// the only door.
//
// ENML's version differs in one respect that follows from M5.5. That milestone
// records what the platform's root of trust actually provides and refuses two
// claims it cannot back. Sealing inherits the discipline: a platform that cannot
// measure cannot seal, and the honest response is to refuse rather than to seal
// against nothing and describe the result as bound.
namespace os::boot {

// A digest over the measurements a key is bound to. SHA-256, which is already
// the tree's measurement hash, and is the one primitive the attestation design
// also needs - the same choice that keeps the mandatory primitive count at one.
inline constexpr std::size_t measurement_digest_bytes = 32U;
using MeasurementDigest = std::array<std::uint8_t, measurement_digest_bytes>;

namespace sealing_errors {
inline constexpr std::uint32_t not_sealed = 1U;
inline constexpr std::uint32_t already_sealed = 2U;
// The platform cannot measure, so nothing it produced could be bound to.
inline constexpr std::uint32_t unmeasured_platform = 3U;
// The current boot state is not the one this key was sealed to.
inline constexpr std::uint32_t measurement_mismatch = 4U;
// A digest of all zeroes is what an unmeasured platform reports, and is not a
// measurement. Sealing to it would bind a key to nothing while looking bound.
inline constexpr std::uint32_t degenerate_digest = 5U;
} // namespace sealing_errors

// What the platform can actually promise about measurement, on the M5.5 model.
enum class MeasurementSupport : std::uint8_t {
    // Early boot measures every link and the measurements reach here intact.
    measured = 1U,
    // No usable measurement. Sealing is refused rather than faked.
    unmeasured = 2U,
};

[[nodiscard]] constexpr bool valid_measurement_support(MeasurementSupport value) noexcept {
    switch (value) {
    case MeasurementSupport::measured:
    case MeasurementSupport::unmeasured:
        return true;
    }
    return false;
}

class SealingPolicy final {
public:
    SealingPolicy() noexcept = default;
    explicit SealingPolicy(MeasurementSupport support) noexcept;

    // Binds to the boot state that produced this digest.
    //
    // Refused on a platform that cannot measure, and refused for an all-zero
    // digest - which is what an unmeasured platform reports and would bind a key
    // to nothing while looking bound. Refused a second time, because silently
    // re-sealing to whatever is running now is how an attacker who has already
    // replaced the OS gets the binding re-issued in their favour.
    [[nodiscard]] os::core::Result<void> seal(const MeasurementDigest& digest) noexcept;

    // Whether the key may be released to the boot state now running.
    //
    // The comparison is constant-time. A digest is not a secret, but the amount
    // of it an attacker has guessed correctly is information, and a comparison
    // that stops at the first differing byte hands over the rest one byte at a
    // time. `os::core::constant_time_equal` exists for exactly this and the
    // reasoning is written out in `os/core/secret.hpp`.
    [[nodiscard]] os::core::Result<void> may_release(
        const MeasurementDigest& current) const noexcept;

    // Discards the binding. Used when a key is being destroyed anyway - the
    // duress path and the erasure threshold both reach here - so that a stale
    // binding cannot outlive the key it bound.
    void unseal() noexcept;

    [[nodiscard]] bool sealed() const noexcept;
    [[nodiscard]] bool sealing_available() const noexcept;

private:
    MeasurementDigest expected_ {};
    MeasurementSupport support_ {MeasurementSupport::unmeasured};
    bool sealed_ {false};
};

} // namespace os::boot
