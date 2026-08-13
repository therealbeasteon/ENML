#pragma once

#include <cstdint>

#include <os/core/result.hpp>

namespace os::storage {

// Crash-consistent publication of an encrypted object is a state machine, not a
// single rename. Durable ciphertext, the matching wrapped object key, and the
// namespace pointer must become visible in an order that never leaves a
// committed name referring to unauthenticated/unrecoverable material.
enum class ProtectedPublicationPhase : std::uint8_t {
    idle = 0U,
    key_durable = 1U,
    ciphertext_durable = 2U,
    commit_record_durable = 3U,
    namespace_published = 4U,
    retired = 5U,
};

[[nodiscard]] constexpr bool valid_publication_phase(ProtectedPublicationPhase phase) noexcept {
    switch (phase) {
    case ProtectedPublicationPhase::idle:
    case ProtectedPublicationPhase::key_durable:
    case ProtectedPublicationPhase::ciphertext_durable:
    case ProtectedPublicationPhase::commit_record_durable:
    case ProtectedPublicationPhase::namespace_published:
    case ProtectedPublicationPhase::retired:
        return true;
    }
    return false;
}

namespace protected_publication_errors {
inline constexpr std::uint32_t invalid_transition = 1U;
inline constexpr std::uint32_t recovery_required = 2U;
} // namespace protected_publication_errors

[[nodiscard]] constexpr os::core::Error publication_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::storage, 0x700U + code);
}

// Pure policy core used by system.storage and power-cut tests. The caller owns
// actual fsync/barrier/atomic-name operations; this object defines which durable
// evidence must exist before the next step is legal.
class ProtectedPublication final {
public:
    [[nodiscard]] ProtectedPublicationPhase phase() const noexcept { return phase_; }

    [[nodiscard]] os::core::Result<void> mark_key_durable() noexcept {
        return advance(ProtectedPublicationPhase::idle, ProtectedPublicationPhase::key_durable);
    }
    [[nodiscard]] os::core::Result<void> mark_ciphertext_durable() noexcept {
        return advance(ProtectedPublicationPhase::key_durable,
                       ProtectedPublicationPhase::ciphertext_durable);
    }
    [[nodiscard]] os::core::Result<void> mark_commit_record_durable() noexcept {
        return advance(ProtectedPublicationPhase::ciphertext_durable,
                       ProtectedPublicationPhase::commit_record_durable);
    }
    [[nodiscard]] os::core::Result<void> mark_namespace_published() noexcept {
        return advance(ProtectedPublicationPhase::commit_record_durable,
                       ProtectedPublicationPhase::namespace_published);
    }
    [[nodiscard]] os::core::Result<void> mark_retired() noexcept {
        return advance(ProtectedPublicationPhase::namespace_published,
                       ProtectedPublicationPhase::retired);
    }

    // Recovery never guesses. Before the namespace is published, incomplete new
    // material may be discarded. Once a durable commit record exists, recovery
    // must finish publication or explicitly roll it back using that record; it
    // may not expose plaintext or silently select a stale generation.
    [[nodiscard]] constexpr bool may_discard_staging_after_crash() const noexcept {
        return phase_ == ProtectedPublicationPhase::idle ||
               phase_ == ProtectedPublicationPhase::key_durable ||
               phase_ == ProtectedPublicationPhase::ciphertext_durable;
    }

    [[nodiscard]] constexpr bool requires_recovery_after_crash() const noexcept {
        return phase_ == ProtectedPublicationPhase::commit_record_durable ||
               phase_ == ProtectedPublicationPhase::namespace_published;
    }

private:
    [[nodiscard]] os::core::Result<void> advance(
        ProtectedPublicationPhase expected,
        ProtectedPublicationPhase next) noexcept {
        if (phase_ != expected) {
            return publication_error(protected_publication_errors::invalid_transition);
        }
        phase_ = next;
        return {};
    }

    ProtectedPublicationPhase phase_ {ProtectedPublicationPhase::idle};
};

} // namespace os::storage
