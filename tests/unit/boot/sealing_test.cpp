#include <cstdint>
#include <cstdio>

#include <os/boot/sealing.hpp>
#include <os/core/error.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "sealing: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& r, std::uint32_t code) {
    return !r && r.error().domain == os::core::ErrorDomain::security && r.error().code == code;
}

os::boot::MeasurementDigest digest_of(std::uint8_t seed) {
    os::boot::MeasurementDigest d{};
    for (std::size_t i = 0U; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
    }
    return d;
}

using os::boot::MeasurementSupport;
using os::boot::SealingPolicy;

} // namespace

int main() {
    // The ordinary case: bound to a boot state, released only to that one.
    {
        SealingPolicy policy{MeasurementSupport::measured};
        if (!check(policy.sealing_available(), "a measured platform reported otherwise")) return 1;
        if (!check(!policy.sealed(), "a fresh policy claimed to be sealed")) return 1;

        const auto genuine = digest_of(1U);
        if (!check(static_cast<bool>(policy.seal(genuine)), "sealing refused")) return 1;
        if (!check(policy.sealed(), "sealing was not recorded")) return 1;
        if (!check(static_cast<bool>(policy.may_release(genuine)),
                   "the boot state it was sealed to was refused")) return 1;

        // One different byte is a different boot state. This is the case that
        // matters: an attacker who replaces the OS gets a key that does not work
        // rather than a prompt they can grind against.
        auto tampered = genuine;
        tampered[17] = static_cast<std::uint8_t>(tampered[17] ^ 0x01U);
        if (!check(refused(policy.may_release(tampered),
                           os::boot::sealing_errors::measurement_mismatch),
                   "a modified boot state was allowed to release the key")) return 1;
    }

    // A platform that cannot measure cannot seal, and says so rather than
    // producing a binding to nothing. M5.5's discipline: refuse the claim the
    // hardware cannot back.
    {
        SealingPolicy policy{MeasurementSupport::unmeasured};
        if (!check(!policy.sealing_available(), "an unmeasured platform offered sealing")) {
            return 1;
        }
        if (!check(refused(policy.seal(digest_of(2U)),
                           os::boot::sealing_errors::unmeasured_platform),
                   "an unmeasured platform sealed a key")) return 1;

        // And an unrecognised capability degrades to unmeasured rather than to
        // measured, because failing the other way manufactures a false binding.
        SealingPolicy garbled{static_cast<MeasurementSupport>(0)};
        if (!check(!garbled.sealing_available(),
                   "an invalid capability claimed measurement")) return 1;
    }

    // An all-zero digest is what an unmeasured platform reports. Sealing to it
    // would look bound and be bound to nothing.
    {
        SealingPolicy policy{MeasurementSupport::measured};
        const os::boot::MeasurementDigest zeroes{};
        if (!check(refused(policy.seal(zeroes), os::boot::sealing_errors::degenerate_digest),
                   "a key was sealed to an all-zero measurement")) return 1;
    }

    // Unsealed fails closed. Treating "never bound" as "binding satisfied" would
    // make every other rule here decorative.
    {
        SealingPolicy policy{MeasurementSupport::measured};
        if (!check(refused(policy.may_release(digest_of(3U)),
                           os::boot::sealing_errors::not_sealed),
                   "an unsealed policy released a key")) return 1;
    }

    // Re-sealing is refused. Otherwise an attacker who has already replaced the
    // OS only has to ask for the binding to be re-issued in their favour.
    {
        SealingPolicy policy{MeasurementSupport::measured};
        (void)policy.seal(digest_of(4U));
        if (!check(refused(policy.seal(digest_of(5U)),
                           os::boot::sealing_errors::already_sealed),
                   "a sealed key was re-bound to a different boot state")) return 1;
        if (!check(static_cast<bool>(policy.may_release(digest_of(4U))),
                   "the original binding did not survive the refused re-seal")) return 1;

        // Explicitly discarding the binding is available, for the paths that are
        // destroying the key anyway.
        policy.unseal();
        if (!check(!policy.sealed(), "unseal did not clear the binding")) return 1;
        if (!check(static_cast<bool>(policy.seal(digest_of(5U))),
                   "sealing after an explicit unseal was refused")) return 1;
    }

    return 0;
}
