#include <os/boot/state.hpp>

#include <os/core/error.hpp>

namespace os::boot {
namespace {

[[nodiscard]] constexpr os::core::Error boot_error(std::uint32_t code) noexcept {
    // Boot state is security-domain evidence, not a storage or ipc fault.
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

// Field offsets. Written out rather than derived from a struct so the format is
// defined by this file and not by whatever the compiler chose to lay out.
inline constexpr std::size_t offset_magic = 0U;
inline constexpr std::size_t offset_header_size = 4U;
inline constexpr std::size_t offset_version = 6U;
inline constexpr std::size_t offset_lifecycle = 8U;
inline constexpr std::size_t offset_verification = 9U;
inline constexpr std::size_t offset_reserved0 = 10U;
inline constexpr std::size_t offset_security_version = 12U;
inline constexpr std::size_t offset_stage_count = 20U;
inline constexpr std::size_t offset_reserved1 = 22U;
inline constexpr std::size_t offset_capabilities = 24U;
inline constexpr std::size_t offset_reserved3 = 28U;

inline constexpr std::size_t stage_offset_kind = 0U;
inline constexpr std::size_t stage_offset_algorithm = 2U;
inline constexpr std::size_t stage_offset_reserved0 = 3U;
inline constexpr std::size_t stage_offset_reserved1 = 4U;
inline constexpr std::size_t stage_offset_digest = 8U;

[[nodiscard]] std::uint16_t read_u16_le(os::core::ByteSpan bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(os::core::ByteSpan bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
            << static_cast<unsigned>(index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64_le(os::core::ByteSpan bytes, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
            << static_cast<unsigned>(index * 8U);
    }
    return value;
}

void write_u16_le(os::core::MutableByteSpan bytes, std::size_t offset, std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write_u32_le(os::core::MutableByteSpan bytes, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> static_cast<unsigned>(index * 8U)) & 0xFFU);
    }
}

void write_u64_le(os::core::MutableByteSpan bytes, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> static_cast<unsigned>(index * 8U)) & 0xFFU);
    }
}

// Unknown discriminants are rejected, never defaulted. An attacker who can pick
// an unhandled value otherwise picks the code path.
[[nodiscard]] bool valid_lifecycle(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(LifecycleState::open) ||
        raw == static_cast<std::uint8_t>(LifecycleState::closed);
}

[[nodiscard]] bool valid_verification(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(VerificationResult::failed) ||
        raw == static_cast<std::uint8_t>(VerificationResult::verified);
}

[[nodiscard]] bool valid_stage_kind(std::uint16_t raw) noexcept {
    return raw >= static_cast<std::uint16_t>(BootStageKind::first_stage) &&
        raw <= static_cast<std::uint16_t>(BootStageKind::root_filesystem);
}

[[nodiscard]] bool valid_digest_algorithm(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(DigestAlgorithm::sha_256);
}

// Every link the chain defines. A closed, verified device must measure all of
// them; a partially measured chain on a production device is the state an
// attacker wants to manufacture.
inline constexpr BootStageKind required_closed_stages[] {
    BootStageKind::first_stage,
    BootStageKind::bootloader,
    BootStageKind::kernel,
    BootStageKind::configuration,
    BootStageKind::root_filesystem,
};

} // namespace

os::core::Result<BootStageMeasurement>
BootStateV1::stage(std::size_t index) const noexcept {
    if (index >= stage_count_) {
        return boot_error(errors::malformed_record);
    }
    return stages_[index];
}

os::core::Result<BootStageMeasurement>
BootStateV1::stage_of_kind(BootStageKind kind) const noexcept {
    for (std::size_t index = 0U; index < stage_count_; ++index) {
        if (stages_[index].kind == kind) {
            return stages_[index];
        }
    }
    return boot_error(errors::malformed_record);
}

os::core::Result<BootStateV1>
parse_boot_state_v1(os::core::ByteSpan encoded) noexcept {
    if (encoded.size() < boot_state_header_bytes_v1) {
        return boot_error(errors::malformed_record);
    }

    for (std::size_t index = 0U; index < boot_state_magic.size(); ++index) {
        if (encoded[offset_magic + index] != boot_state_magic[index]) {
            return boot_error(errors::malformed_record);
        }
    }

    if (read_u16_le(encoded, offset_header_size) != boot_state_header_bytes_v1) {
        return boot_error(errors::malformed_record);
    }
    if (read_u16_le(encoded, offset_version) != boot_state_version_v1) {
        return boot_error(errors::unsupported_version);
    }
    if (read_u16_le(encoded, offset_reserved0) != 0U ||
        read_u16_le(encoded, offset_reserved1) != 0U ||
        read_u32_le(encoded, offset_reserved3) != 0U) {
        return boot_error(errors::reserved_not_zero);
    }

    const auto raw_lifecycle = std::to_integer<std::uint8_t>(encoded[offset_lifecycle]);
    if (!valid_lifecycle(raw_lifecycle)) {
        return boot_error(errors::unknown_lifecycle);
    }
    const auto raw_verification = std::to_integer<std::uint8_t>(encoded[offset_verification]);
    if (!valid_verification(raw_verification)) {
        return boot_error(errors::unknown_verification);
    }

    const auto stage_count = read_u16_le(encoded, offset_stage_count);
    if (stage_count > max_boot_stages) {
        return boot_error(errors::too_many_stages);
    }

    const auto expected_size = static_cast<std::size_t>(boot_state_header_bytes_v1) +
        (static_cast<std::size_t>(stage_count) *
         static_cast<std::size_t>(boot_stage_record_bytes_v1));
    if (encoded.size() != expected_size) {
        return boot_error(errors::trailing_bytes);
    }

    BootStateV1 state {};
    state.lifecycle_ = static_cast<LifecycleState>(raw_lifecycle);
    state.verification_ = static_cast<VerificationResult>(raw_verification);
    state.security_version_ = read_u64_le(encoded, offset_security_version);

    const auto capabilities = read_u32_le(encoded, offset_capabilities);
    if ((capabilities & ~known_platform_capabilities) != 0U) {
        return boot_error(errors::unknown_capability);
    }
    state.capabilities_ = capabilities;
    state.stage_count_ = stage_count;

    for (std::size_t index = 0U; index < stage_count; ++index) {
        const auto base = static_cast<std::size_t>(boot_state_header_bytes_v1) +
            (index * static_cast<std::size_t>(boot_stage_record_bytes_v1));

        const auto raw_kind = read_u16_le(encoded, base + stage_offset_kind);
        if (!valid_stage_kind(raw_kind)) {
            return boot_error(errors::unknown_stage_kind);
        }
        const auto raw_algorithm =
            std::to_integer<std::uint8_t>(encoded[base + stage_offset_algorithm]);
        if (!valid_digest_algorithm(raw_algorithm)) {
            return boot_error(errors::unknown_digest_algorithm);
        }
        if (std::to_integer<std::uint8_t>(encoded[base + stage_offset_reserved0]) != 0U ||
            read_u32_le(encoded, base + stage_offset_reserved1) != 0U) {
            return boot_error(errors::reserved_not_zero);
        }

        auto& measurement = state.stages_[index];
        measurement.kind = static_cast<BootStageKind>(raw_kind);
        measurement.algorithm = static_cast<DigestAlgorithm>(raw_algorithm);
        for (std::size_t byte = 0U; byte < sha256_digest_bytes; ++byte) {
            measurement.digest[byte] = encoded[base + stage_offset_digest + byte];
        }

        // A repeated kind means two different digests claim the same link.
        // Accepting that would let a caller pick whichever one it liked.
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (state.stages_[earlier].kind == measurement.kind) {
                return boot_error(errors::duplicate_stage);
            }
        }
    }

    if (state.verification_ == VerificationResult::verified) {
        if (state.stage_count_ == 0U) {
            return boot_error(errors::incoherent_state);
        }
        if (state.lifecycle_ == LifecycleState::closed) {
            for (const auto required : required_closed_stages) {
                if (!state.stage_of_kind(required)) {
                    return boot_error(errors::incoherent_state);
                }
            }
            // A production device asserting a verified boot without a first
            // stage software cannot rewrite is asserting something the platform
            // cannot support. Hardware neutrality means adapting to what a
            // platform provides, not accepting a claim it cannot back.
            if (!state.provides(PlatformCapability::immutable_first_stage)) {
                return boot_error(errors::unsupported_claim);
            }
        }
    }

    // Rollback resistance is a claim about a counter that cannot move
    // backwards. Without one, a security version is a number, and treating it
    // as a guarantee is how rollback protection becomes decorative.
    if (state.security_version_ != 0U &&
        !state.provides(PlatformCapability::monotonic_counter)) {
        return boot_error(errors::unsupported_claim);
    }

    return state;
}

os::core::Result<std::size_t>
encode_boot_state_v1(const BootStateV1& state, os::core::MutableByteSpan output) noexcept {
    const auto total = static_cast<std::size_t>(boot_state_header_bytes_v1) +
        (state.stage_count() * static_cast<std::size_t>(boot_stage_record_bytes_v1));
    if (output.size() < total) {
        return boot_error(errors::malformed_record);
    }

    for (std::size_t index = 0U; index < total; ++index) {
        output[index] = std::byte{0};
    }
    for (std::size_t index = 0U; index < boot_state_magic.size(); ++index) {
        output[offset_magic + index] = boot_state_magic[index];
    }
    write_u16_le(output, offset_header_size, boot_state_header_bytes_v1);
    write_u16_le(output, offset_version, boot_state_version_v1);
    output[offset_lifecycle] = static_cast<std::byte>(state.lifecycle());
    output[offset_verification] = static_cast<std::byte>(state.verification());
    write_u64_le(output, offset_security_version, state.security_version());
    write_u32_le(output, offset_capabilities, state.capabilities());
    write_u16_le(output, offset_stage_count, static_cast<std::uint16_t>(state.stage_count()));

    for (std::size_t index = 0U; index < state.stage_count(); ++index) {
        auto measurement = state.stage(index);
        if (!measurement) {
            return measurement.error();
        }
        const auto base = static_cast<std::size_t>(boot_state_header_bytes_v1) +
            (index * static_cast<std::size_t>(boot_stage_record_bytes_v1));
        write_u16_le(
            output, base + stage_offset_kind,
            static_cast<std::uint16_t>(measurement.value().kind));
        output[base + stage_offset_algorithm] =
            static_cast<std::byte>(measurement.value().algorithm);
        for (std::size_t byte = 0U; byte < sha256_digest_bytes; ++byte) {
            output[base + stage_offset_digest + byte] = measurement.value().digest[byte];
        }
    }

    return total;
}

os::core::Result<void>
BootStateBuilder::add_stage(
    BootStageKind kind,
    DigestAlgorithm algorithm,
    os::core::ByteSpan digest) noexcept {
    if (stage_count_ >= max_boot_stages) {
        return boot_error(errors::too_many_stages);
    }
    if (digest.size() != sha256_digest_bytes) {
        return boot_error(errors::malformed_record);
    }
    if (!valid_stage_kind(static_cast<std::uint16_t>(kind))) {
        return boot_error(errors::unknown_stage_kind);
    }
    if (!valid_digest_algorithm(static_cast<std::uint8_t>(algorithm))) {
        return boot_error(errors::unknown_digest_algorithm);
    }
    for (std::size_t index = 0U; index < stage_count_; ++index) {
        if (stages_[index].kind == kind) {
            return boot_error(errors::duplicate_stage);
        }
    }

    auto& measurement = stages_[stage_count_];
    measurement.kind = kind;
    measurement.algorithm = algorithm;
    for (std::size_t byte = 0U; byte < sha256_digest_bytes; ++byte) {
        measurement.digest[byte] = digest[byte];
    }
    ++stage_count_;
    return {};
}

os::core::Result<std::size_t>
BootStateBuilder::encode(os::core::MutableByteSpan output) const noexcept {
    const auto total = static_cast<std::size_t>(boot_state_header_bytes_v1) +
        (stage_count_ * static_cast<std::size_t>(boot_stage_record_bytes_v1));
    if (output.size() < total) {
        return boot_error(errors::malformed_record);
    }

    for (std::size_t index = 0U; index < total; ++index) {
        output[index] = std::byte{0};
    }
    for (std::size_t index = 0U; index < boot_state_magic.size(); ++index) {
        output[offset_magic + index] = boot_state_magic[index];
    }
    write_u16_le(output, offset_header_size, boot_state_header_bytes_v1);
    write_u16_le(output, offset_version, boot_state_version_v1);
    output[offset_lifecycle] = static_cast<std::byte>(lifecycle_);
    output[offset_verification] = static_cast<std::byte>(verification_);
    write_u64_le(output, offset_security_version, security_version_);
    write_u32_le(output, offset_capabilities, capabilities_);
    write_u16_le(output, offset_stage_count, static_cast<std::uint16_t>(stage_count_));

    for (std::size_t index = 0U; index < stage_count_; ++index) {
        const auto base = static_cast<std::size_t>(boot_state_header_bytes_v1) +
            (index * static_cast<std::size_t>(boot_stage_record_bytes_v1));
        write_u16_le(
            output, base + stage_offset_kind,
            static_cast<std::uint16_t>(stages_[index].kind));
        output[base + stage_offset_algorithm] =
            static_cast<std::byte>(stages_[index].algorithm);
        for (std::size_t byte = 0U; byte < sha256_digest_bytes; ++byte) {
            output[base + stage_offset_digest + byte] = stages_[index].digest[byte];
        }
    }

    return total;
}

} // namespace os::boot
