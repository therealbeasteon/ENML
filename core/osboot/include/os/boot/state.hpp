#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>

// Verified boot state: the evidence a completed boot chain hands to the OS.
//
// M5.0 establishes that every ENML security property is conditional on the
// running code being the code we shipped. This record is where that condition
// becomes an object the system can reason about instead of an assumption.
//
// It is deliberately built now, before a target platform exists, because the
// shape of the evidence is platform-independent even though its production is
// not. A bootloader integration fills this in; nothing above it needs to change
// when the platform is chosen.
//
// Design rules, inherited from the existing wire and registry substrates:
// explicit little-endian, no native C++ layout serialization, fixed capacity,
// reserved fields rejected when nonzero, and unknown enumerated values rejected
// rather than defaulted. The last rule matters most here - an attacker who can
// choose an unhandled discriminant chooses the code path.
//
// The record is NOT self-authenticating and must not be mistaken for evidence
// on its own. Structural corruption is rejected, but altering the security
// version yields a well-formed record with a different counter and the parser
// cannot tell. Integrity comes from the record being produced by trusted early
// boot and never crossing an untrusted boundary. If it ever needs to cross one
// - to an attestation verifier, say - it must be signed, and that signature is
// a separate design with its own threat model.
namespace os::boot {

inline constexpr std::array<std::byte, 4> boot_state_magic {
    std::byte{'E'}, std::byte{'B'}, std::byte{'S'}, std::byte{'1'}
};

inline constexpr std::uint16_t boot_state_version_v1 = 1U;
inline constexpr std::uint16_t boot_state_header_bytes_v1 = 32U;
inline constexpr std::uint16_t boot_stage_record_bytes_v1 = 40U;
inline constexpr std::size_t sha256_digest_bytes = 32U;

// A boot chain with more links than this is not a chain we understand. The
// ceiling is a rejection criterion, not a buffer size.
inline constexpr std::size_t max_boot_stages = 8U;

inline constexpr std::size_t max_boot_state_bytes =
    static_cast<std::size_t>(boot_state_header_bytes_v1) +
    (max_boot_stages * static_cast<std::size_t>(boot_stage_record_bytes_v1));

namespace errors {
inline constexpr std::uint32_t malformed_record = 1U;
inline constexpr std::uint32_t unsupported_version = 2U;
inline constexpr std::uint32_t reserved_not_zero = 3U;
inline constexpr std::uint32_t unknown_lifecycle = 4U;
inline constexpr std::uint32_t unknown_verification = 5U;
inline constexpr std::uint32_t unknown_stage_kind = 6U;
inline constexpr std::uint32_t unknown_digest_algorithm = 7U;
inline constexpr std::uint32_t too_many_stages = 8U;
inline constexpr std::uint32_t duplicate_stage = 9U;
inline constexpr std::uint32_t trailing_bytes = 10U;
inline constexpr std::uint32_t incoherent_state = 11U;
} // namespace errors

// Zero is never a valid discriminant. An all-zero buffer must not decode to a
// meaningful state, and "unset" must not be confusable with any real value.
enum class LifecycleState : std::uint8_t {
    open = 1U,    // development; verification may be incomplete
    closed = 2U,  // production; no bypass path may exist
};

enum class VerificationResult : std::uint8_t {
    failed = 1U,
    verified = 2U,
};

enum class BootStageKind : std::uint16_t {
    first_stage = 1U,
    bootloader = 2U,
    kernel = 3U,
    configuration = 4U,   // device tree, boot arguments, and their allow-list
    root_filesystem = 5U,
};

enum class DigestAlgorithm : std::uint8_t {
    sha_256 = 1U,
};

struct BootStageMeasurement final {
    BootStageKind kind {BootStageKind::first_stage};
    DigestAlgorithm algorithm {DigestAlgorithm::sha_256};
    std::array<std::byte, sha256_digest_bytes> digest {};

    [[nodiscard]] friend bool
    operator==(const BootStageMeasurement&, const BootStageMeasurement&) = default;
};

// The default state is an unverified one.
//
// This is the single most important property of the type. Absence of evidence
// is failure, never success: a caller that forgets to parse, or parses and
// ignores the error, holds a state that says the boot was not verified. There
// is no constructor that produces a verified state from nothing.
class BootStateV1 final {
public:
    BootStateV1() noexcept = default;

    [[nodiscard]] bool verified() const noexcept {
        return verification_ == VerificationResult::verified;
    }

    [[nodiscard]] LifecycleState lifecycle() const noexcept { return lifecycle_; }
    [[nodiscard]] VerificationResult verification() const noexcept { return verification_; }

    // The rollback counter. Only meaningful on a verified state; a caller must
    // check verified() first, which is why this deliberately does not return a
    // value that reads as safe on its own.
    [[nodiscard]] std::uint64_t security_version() const noexcept { return security_version_; }

    [[nodiscard]] std::size_t stage_count() const noexcept { return stage_count_; }

    [[nodiscard]] os::core::Result<BootStageMeasurement>
    stage(std::size_t index) const noexcept;

    // Looks up a stage by kind. Returns an error when absent rather than a
    // zeroed measurement, so a missing link cannot be mistaken for a measured
    // one whose digest happens to be zero.
    [[nodiscard]] os::core::Result<BootStageMeasurement>
    stage_of_kind(BootStageKind kind) const noexcept;

private:
    friend os::core::Result<BootStateV1> parse_boot_state_v1(os::core::ByteSpan) noexcept;

    LifecycleState lifecycle_ {LifecycleState::open};
    VerificationResult verification_ {VerificationResult::failed};
    std::uint64_t security_version_ {0};
    std::size_t stage_count_ {0};
    std::array<BootStageMeasurement, max_boot_stages> stages_ {};
};

// Decodes and validates a boot state record.
//
// Fails closed on: bad magic, wrong header size or version, nonzero reserved
// fields, unknown lifecycle/verification/stage-kind/digest discriminants, more
// stages than the ceiling, duplicate stage kinds, trailing bytes, and states
// that are internally incoherent (see below).
//
// Coherence rules enforced here rather than left to callers:
//   - a verified state must carry at least one measured stage;
//   - a verified state in the closed lifecycle must measure every link the
//     chain defines, because a closed device with a partially measured chain is
//     the exact state an attacker wants to manufacture;
//   - a failed state may carry measurements, since knowing which link broke is
//     useful, but it can never satisfy verified().
[[nodiscard]] os::core::Result<BootStateV1>
parse_boot_state_v1(os::core::ByteSpan encoded) noexcept;

// Encodes a boot state. Present so tests and future bootloader integration
// share one definition of the format rather than two that drift.
[[nodiscard]] os::core::Result<std::size_t>
encode_boot_state_v1(const BootStateV1& state, os::core::MutableByteSpan output) noexcept;

// Builder used by trusted early boot and by tests. Separate from BootStateV1 so
// that holding a state never implies the ability to have fabricated one.
class BootStateBuilder final {
public:
    [[nodiscard]] os::core::Result<void>
    add_stage(BootStageKind kind, DigestAlgorithm algorithm, os::core::ByteSpan digest) noexcept;

    void set_lifecycle(LifecycleState lifecycle) noexcept { lifecycle_ = lifecycle; }
    void set_verification(VerificationResult result) noexcept { verification_ = result; }
    void set_security_version(std::uint64_t version) noexcept { security_version_ = version; }

    // Serializes directly. There is deliberately no build() returning a
    // BootStateV1: a state must come from parsing a record, so that every
    // verified state in the system has passed the same validation.
    [[nodiscard]] os::core::Result<std::size_t>
    encode(os::core::MutableByteSpan output) const noexcept;

private:
    LifecycleState lifecycle_ {LifecycleState::open};
    VerificationResult verification_ {VerificationResult::failed};
    std::uint64_t security_version_ {0};
    std::size_t stage_count_ {0};
    std::array<BootStageMeasurement, max_boot_stages> stages_ {};
};

} // namespace os::boot
