#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/boot/attestation.hpp>
#include <os/core/error.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "attestation: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::security &&
        result.error().code == code;
}

using os::boot::AttestationPolicy;
using os::boot::LinkagePolicy;

constexpr os::boot::VerifierTag bank = 0x1111U;
constexpr os::boot::VerifierTag website = 0x2222U;
constexpr os::boot::AttestationEpoch first_epoch = 100U;
constexpr os::boot::AttestationEpoch later_epoch = 101U;

} // namespace

int main() {
    // One unlinkable attestation per verifier per epoch, and the second is
    // refused. Producing it is the operation the traceable construction punishes
    // by de-anonymizing the device, so the system declines rather than
    // discovering afterwards that it identified its owner.
    {
        AttestationPolicy policy;
        if (!check(static_cast<bool>(policy.begin_epoch(first_epoch)), "epoch refused")) return 1;

        auto first = policy.authorise(website, LinkagePolicy::unlinkable);
        if (!check(static_cast<bool>(first), "the first attestation was refused")) return 1;
        if (!check(first.value().epoch == first_epoch, "wrong epoch on the grant")) return 1;
        if (!check(!first.value().reveals_continuity,
                   "an unlinkable attestation reported revealing continuity")) return 1;

        if (!check(refused(policy.authorise(website, LinkagePolicy::unlinkable),
                           os::boot::attestation_errors::would_deanonymize),
                   "a second unlinkable attestation would have de-anonymized the device")) {
            return 1;
        }

        // A different verifier in the same epoch is a different tag, and is fine.
        if (!check(static_cast<bool>(policy.authorise(bank, LinkagePolicy::unlinkable)),
                   "attesting to a second verifier was refused")) return 1;
        if (!check(policy.attested_verifier_count() == 2U, "wrong verifier count")) return 1;
    }

    // Linkage is the user's choice, and where they have made it, repetition
    // costs nothing that has not already been spent - the verifier can already
    // recognise the device.
    {
        AttestationPolicy policy;
        (void)policy.begin_epoch(first_epoch);

        auto granted = policy.authorise(bank, LinkagePolicy::user_linked);
        if (!check(static_cast<bool>(granted), "a linked attestation was refused")) return 1;
        if (!check(granted.value().reveals_continuity,
                   "a linked attestation did not report revealing continuity")) return 1;

        auto again = policy.authorise(bank, LinkagePolicy::user_linked);
        if (!check(static_cast<bool>(again), "repeating a linked attestation was refused")) {
            return 1;
        }
        if (!check(again.value().reveals_continuity, "continuity was not reported")) return 1;
    }

    // Escalating a verifier that was attested to anonymously is the same
    // disclosure by a slower route: it tells them the earlier anonymous
    // attestation came from this device.
    {
        AttestationPolicy policy;
        (void)policy.begin_epoch(first_epoch);
        (void)policy.authorise(website, LinkagePolicy::unlinkable);

        if (!check(refused(policy.authorise(website, LinkagePolicy::user_linked),
                           os::boot::attestation_errors::would_deanonymize),
                   "an anonymous verifier was escalated to linked")) return 1;
    }

    // A new epoch is a new tag, so the right to attest anonymously returns.
    {
        AttestationPolicy policy;
        (void)policy.begin_epoch(first_epoch);
        (void)policy.authorise(website, LinkagePolicy::unlinkable);

        if (!check(static_cast<bool>(policy.begin_epoch(later_epoch)), "new epoch refused")) {
            return 1;
        }
        if (!check(policy.attested_verifier_count() == 0U,
                   "the new epoch remembered the old one")) return 1;
        if (!check(static_cast<bool>(policy.authorise(website, LinkagePolicy::unlinkable)),
                   "a new epoch did not restore the right to attest")) return 1;
    }

    // Epochs only move forward. Re-entering one would hand back the right to
    // attest unlinkably to a verifier already attested to in it, and an attacker
    // who can move a clock backwards is exactly who would want that.
    {
        AttestationPolicy policy;
        (void)policy.begin_epoch(later_epoch);
        if (!check(refused(policy.begin_epoch(first_epoch),
                           os::boot::attestation_errors::epoch_regressed),
                   "an epoch was replayed")) return 1;
        if (!check(refused(policy.begin_epoch(later_epoch),
                           os::boot::attestation_errors::epoch_regressed),
                   "the current epoch was re-entered")) return 1;
        if (!check(policy.current_epoch() == later_epoch, "the epoch moved")) return 1;
    }

    // Malformed requests are refused rather than interpreted, and nothing is
    // attested before an epoch has begun.
    {
        AttestationPolicy policy;
        if (!check(refused(policy.authorise(bank, LinkagePolicy::unlinkable),
                           os::boot::attestation_errors::invalid_epoch_id),
                   "attested before any epoch had begun")) return 1;

        (void)policy.begin_epoch(first_epoch);
        if (!check(refused(policy.authorise(os::boot::invalid_verifier,
                                            LinkagePolicy::unlinkable),
                           os::boot::attestation_errors::invalid_verifier_tag),
                   "attested to an invalid verifier")) return 1;
        if (!check(refused(policy.authorise(bank, static_cast<LinkagePolicy>(0)),
                           os::boot::attestation_errors::unknown_linkage),
                   "an unknown linkage policy was accepted")) return 1;
        if (!check(refused(policy.begin_epoch(os::boot::invalid_epoch),
                           os::boot::attestation_errors::invalid_epoch_id),
                   "an invalid epoch was accepted")) return 1;
    }

    // The table has a stated ceiling and refuses rather than overruns.
    {
        AttestationPolicy policy;
        (void)policy.begin_epoch(first_epoch);
        for (std::size_t i = 0U; i < os::boot::max_tracked_verifiers; ++i) {
            const auto verifier = static_cast<os::boot::VerifierTag>(i + 1U);
            if (!check(static_cast<bool>(policy.authorise(verifier, LinkagePolicy::unlinkable)),
                       "attestation refused below the ceiling")) return 1;
        }
        const auto beyond =
            static_cast<os::boot::VerifierTag>(os::boot::max_tracked_verifiers + 1U);
        if (!check(refused(policy.authorise(beyond, LinkagePolicy::unlinkable),
                           os::boot::attestation_errors::verifier_limit),
                   "the table grew past its ceiling")) return 1;
        if (!check(policy.has_attested(1U), "a recorded verifier was forgotten")) return 1;
    }

    return 0;
}
