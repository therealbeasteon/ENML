#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

// Attestation epoch and linkage policy.
//
// `docs/M7_4_ANONYMOUS_ATTESTATION.md` is the design. This file is the part that
// is ENML's own and that can be tested with no cryptography present at all.
//
// The reference construction ENML is aiming at - one-time traceable ring
// signatures - gives anonymity with no group manager, needs only hash
// evaluations, and is post-quantum resistant for the same reason. It carries one
// precondition, and it enforces that precondition by *punishment*: a signer who
// signs two different messages under the same tag is de-anonymized.
//
// That inversion is what this file exists for. A precondition enforced by
// consequences is a precondition that eventually gets violated, and the
// consequence here is the permanent loss of the property the whole scheme was
// chosen for. So the operating system refuses the second attestation rather than
// letting the mathematics punish it. The cryptography defines what must not
// happen; making it not happen is the OS's job.
//
// Two things are deliberately absent.
//
// The signature. It is a provider, exactly as M2.4 put the AEAD primitive behind
// one and kept the hierarchy, envelope and rotation policy above it. That split
// is what lets the primitive be replaced when the post-quantum picture settles
// without touching policy.
//
// Any reading of `BootStateV1`. What is worth attesting, and whether the
// platform can back it, belongs to the caller that holds the state - M5.5
// already refuses claims the platform cannot support, and duplicating that
// judgement here would make two state machines that know about each other.
namespace os::boot {

// How many verifiers a device can be attesting to within one epoch.
//
// A ceiling rather than a target, so the table underneath is a fixed array with
// no allocator - the same discipline the kernel tables are held to, and for the
// same reason: this runs on a phone.
inline constexpr std::size_t max_tracked_verifiers = 32U;

// The tag under which one-time traceability holds. Signing twice under one tag
// is what de-anonymizes; zero is never a valid epoch.
using AttestationEpoch = std::uint64_t;
inline constexpr AttestationEpoch invalid_epoch = 0U;

// Who the attestation is for. Opaque - this policy only ever compares two of
// them, and a verifier identity it could interpret would be one it could leak.
using VerifierTag = std::uint64_t;
inline constexpr VerifierTag invalid_verifier = 0U;

namespace attestation_errors {
inline constexpr std::uint32_t invalid_epoch_id = 1U;
inline constexpr std::uint32_t invalid_verifier_tag = 2U;
inline constexpr std::uint32_t unknown_linkage = 3U;
// A second unlinkable attestation in one epoch to one verifier. Refused because
// producing it would de-anonymize the device.
inline constexpr std::uint32_t would_deanonymize = 4U;
// An epoch older than the one in progress. Epochs only move forward, or a
// replayed epoch would reset the one-time guarantee.
inline constexpr std::uint32_t epoch_regressed = 5U;
inline constexpr std::uint32_t verifier_limit = 6U;
} // namespace attestation_errors

// Whether the user has agreed that this verifier may recognise the device again.
enum class LinkagePolicy : std::uint8_t {
    // Every attestation to this verifier is unlinkable to the others, which is
    // the default and the only one that costs nothing.
    unlinkable = 1U,
    // The user has chosen to let this verifier link their attestations - a bank
    // that needs to know this is the same device as last week. A choice the user
    // makes, never one the verifier makes.
    user_linked = 2U,
};

[[nodiscard]] constexpr bool valid_linkage(LinkagePolicy value) noexcept {
    switch (value) {
    case LinkagePolicy::unlinkable:
    case LinkagePolicy::user_linked:
        return true;
    }
    return false;
}

// Permission to produce exactly one attestation.
struct AttestationGrant final {
    AttestationEpoch epoch {invalid_epoch};
    VerifierTag verifier {invalid_verifier};
    LinkagePolicy linkage {LinkagePolicy::unlinkable};
    // True when this attestation lets the verifier recognise the device again.
    // Stated on the grant rather than inferred, because a caller that cannot see
    // that continuity is being revealed cannot tell the user.
    bool reveals_continuity {false};

    [[nodiscard]] friend constexpr bool
    operator==(const AttestationGrant&, const AttestationGrant&) = default;
};

class AttestationPolicy final {
public:
    AttestationPolicy() noexcept = default;

    // Moves to a new epoch, forgetting what was attested in the previous one.
    //
    // Epochs only move forward. A replayed epoch would restore the right to
    // attest unlinkably to a verifier that has already been attested to, which
    // is the one-time guarantee being handed back.
    [[nodiscard]] os::core::Result<void> begin_epoch(AttestationEpoch epoch) noexcept;

    // Asks permission to attest, and consumes it.
    //
    // Refuses a second unlinkable attestation to the same verifier in the same
    // epoch, because producing that signature is what would de-anonymize the
    // device. Under user-chosen linkage the device is already recognisable to
    // this verifier by the user's own decision, so repetition costs nothing that
    // has not already been spent.
    os::core::Result<AttestationGrant> authorise(
        VerifierTag verifier,
        LinkagePolicy linkage) noexcept;

    [[nodiscard]] AttestationEpoch current_epoch() const noexcept;
    [[nodiscard]] bool has_attested(VerifierTag verifier) const noexcept;
    [[nodiscard]] std::size_t attested_verifier_count() const noexcept;

private:
    struct Record final {
        VerifierTag verifier {invalid_verifier};
        LinkagePolicy linkage {LinkagePolicy::unlinkable};
        bool occupied {false};
    };

    [[nodiscard]] const Record* find(VerifierTag verifier) const noexcept;

    std::array<Record, max_tracked_verifiers> records_ {};
    std::size_t occupied_ {0U};
    AttestationEpoch epoch_ {invalid_epoch};
};

} // namespace os::boot
