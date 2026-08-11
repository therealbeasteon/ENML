#include <os/boot/sealing.hpp>

#include <cstring>

#include <os/core/error.hpp>
#include <os/core/secret.hpp>

namespace os::boot {
namespace {

[[nodiscard]] constexpr os::core::Error sealing_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] os::core::ByteSpan bytes_of(const MeasurementDigest& digest) noexcept {
    return os::core::ByteSpan{
        reinterpret_cast<const std::byte*>(digest.data()), digest.size()};
}

[[nodiscard]] bool all_zero(const MeasurementDigest& digest) noexcept {
    // Not constant-time on purpose, and it does not need to be: this runs on a
    // value the caller supplied and is about to be told is unusable. The check
    // exists because an unmeasured platform reports zeroes, and sealing to them
    // would produce a key that looks bound and is bound to nothing.
    for (const auto byte : digest) {
        if (byte != 0U) return false;
    }
    return true;
}

} // namespace

SealingPolicy::SealingPolicy(MeasurementSupport support) noexcept
    : support_(valid_measurement_support(support) ? support : MeasurementSupport::unmeasured) {
    // An unrecognised value degrades to unmeasured. Failing the other way would
    // let an out-of-range enum produce a device that believes its keys are bound
    // to a boot state nobody measured.
}

bool SealingPolicy::sealed() const noexcept {
    return sealed_;
}

bool SealingPolicy::sealing_available() const noexcept {
    return support_ == MeasurementSupport::measured;
}

os::core::Result<void> SealingPolicy::seal(const MeasurementDigest& digest) noexcept {
    if (!sealing_available()) {
        return sealing_error(sealing_errors::unmeasured_platform);
    }
    if (all_zero(digest)) {
        return sealing_error(sealing_errors::degenerate_digest);
    }
    if (sealed_) {
        // Re-sealing is refused rather than performed. An attacker who has
        // already replaced the operating system would otherwise only have to ask
        // for the binding to be re-issued, and it would be re-issued in their
        // favour - which is the whole protection handed back on request.
        return sealing_error(sealing_errors::already_sealed);
    }

    expected_ = digest;
    sealed_ = true;
    return {};
}

os::core::Result<void> SealingPolicy::may_release(const MeasurementDigest& current) const noexcept {
    if (!sealed_) {
        // Fails closed. An unsealed policy has no binding to check, and treating
        // "never bound" as "binding satisfied" is the failure that would make
        // every other rule here decorative.
        return sealing_error(sealing_errors::not_sealed);
    }
    if (!os::core::constant_time_equal(bytes_of(expected_), bytes_of(current))) {
        return sealing_error(sealing_errors::measurement_mismatch);
    }
    return {};
}

void SealingPolicy::unseal() noexcept {
    expected_ = MeasurementDigest{};
    sealed_ = false;
}

} // namespace os::boot
