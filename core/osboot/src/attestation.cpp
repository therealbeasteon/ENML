#include <os/boot/attestation.hpp>

#include <os/core/error.hpp>

namespace os::boot {
namespace {

[[nodiscard]] constexpr os::core::Error attestation_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

} // namespace

const AttestationPolicy::Record* AttestationPolicy::find(VerifierTag verifier) const noexcept {
    if (verifier == invalid_verifier) return nullptr;
    for (const auto& record : records_) {
        if (record.occupied && record.verifier == verifier) return &record;
    }
    return nullptr;
}

AttestationEpoch AttestationPolicy::current_epoch() const noexcept {
    return epoch_;
}

bool AttestationPolicy::has_attested(VerifierTag verifier) const noexcept {
    return find(verifier) != nullptr;
}

std::size_t AttestationPolicy::attested_verifier_count() const noexcept {
    return occupied_;
}

os::core::Result<void> AttestationPolicy::begin_epoch(AttestationEpoch epoch) noexcept {
    if (epoch == invalid_epoch) {
        return attestation_error(attestation_errors::invalid_epoch_id);
    }
    // Strictly forward. Re-entering an epoch would hand back the right to attest
    // unlinkably to a verifier already attested to in it, which is the one-time
    // guarantee undone - and an attacker who can set the clock back is exactly
    // who would want that.
    if (epoch <= epoch_) {
        return attestation_error(attestation_errors::epoch_regressed);
    }

    epoch_ = epoch;
    for (auto& record : records_) {
        record = Record{};
    }
    occupied_ = 0U;
    return {};
}

os::core::Result<AttestationGrant> AttestationPolicy::authorise(
    VerifierTag verifier,
    LinkagePolicy linkage) noexcept {
    if (epoch_ == invalid_epoch) {
        return os::core::Result<AttestationGrant>{
            attestation_error(attestation_errors::invalid_epoch_id)};
    }
    if (verifier == invalid_verifier) {
        return os::core::Result<AttestationGrant>{
            attestation_error(attestation_errors::invalid_verifier_tag)};
    }
    if (!valid_linkage(linkage)) {
        return os::core::Result<AttestationGrant>{
            attestation_error(attestation_errors::unknown_linkage)};
    }

    const Record* existing = find(verifier);
    if (existing != nullptr) {
        // The refusal this file exists for.
        //
        // A second unlinkable signature under one tag is what the traceable
        // construction punishes by de-anonymizing the signer. Refusing here is
        // the difference between a precondition the system upholds and one it
        // discovers it has broken.
        if (linkage == LinkagePolicy::unlinkable) {
            return os::core::Result<AttestationGrant>{
                attestation_error(attestation_errors::would_deanonymize)};
        }
        // Escalating an already-unlinkable verifier to linked would reveal that
        // the earlier anonymous attestation came from this device, which is the
        // same disclosure by a slower route.
        if (existing->linkage == LinkagePolicy::unlinkable) {
            return os::core::Result<AttestationGrant>{
                attestation_error(attestation_errors::would_deanonymize)};
        }
        // Already linked by the user's own choice, so the verifier can already
        // recognise this device and repetition spends nothing new.
        return os::core::Result<AttestationGrant>{
            AttestationGrant{epoch_, verifier, linkage, true}};
    }

    for (auto& record : records_) {
        if (record.occupied) continue;
        record = Record{verifier, linkage, true};
        ++occupied_;
        return os::core::Result<AttestationGrant>{AttestationGrant{
            epoch_, verifier, linkage, linkage == LinkagePolicy::user_linked}};
    }
    return os::core::Result<AttestationGrant>{
        attestation_error(attestation_errors::verifier_limit)};
}

} // namespace os::boot
