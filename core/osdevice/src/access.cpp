#include <os/device/access.hpp>

#include <os/core/error.hpp>

namespace os::device {
namespace {

[[nodiscard]] constexpr os::core::Error device_error(std::uint32_t code) noexcept {
    // Device authority is a security-domain concern, not a service fault.
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

// Field offsets. Written out rather than derived from a struct so the format is
// defined by this file and not by whatever the compiler chose to lay out.
inline constexpr std::size_t offset_magic = 0U;
inline constexpr std::size_t offset_version = 4U;
inline constexpr std::size_t offset_header_size = 6U;
inline constexpr std::size_t offset_total_size = 8U;
inline constexpr std::size_t offset_domain = 12U;
inline constexpr std::size_t offset_dma = 13U;
inline constexpr std::size_t offset_grant_count = 14U;
inline constexpr std::size_t offset_reserved0 = 16U;
inline constexpr std::size_t offset_reserved1 = 20U;
inline constexpr std::size_t offset_reserved2 = 24U;

inline constexpr std::size_t grant_offset_base = 0U;
inline constexpr std::size_t grant_offset_length = 8U;
inline constexpr std::size_t grant_offset_access = 16U;
inline constexpr std::size_t grant_offset_reserved = 17U;
inline constexpr std::size_t grant_reserved_bytes = 7U;

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
[[nodiscard]] bool valid_domain(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(ExecutionDomain::kernel_resident) ||
        raw == static_cast<std::uint8_t>(ExecutionDomain::isolated_user);
}

[[nodiscard]] bool valid_dma(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(DmaCapability::none) ||
        raw == static_cast<std::uint8_t>(DmaCapability::iommu_confined) ||
        raw == static_cast<std::uint8_t>(DmaCapability::unconfined);
}

[[nodiscard]] bool valid_access(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(AccessMode::read_only) ||
        raw == static_cast<std::uint8_t>(AccessMode::read_write);
}

// A grant whose end wraps past the top of the address space describes a window
// that does not exist, and every containment check below would be meaningless
// for it. Checked before the value is used, never after.
[[nodiscard]] bool grant_wraps(std::uint64_t base, std::uint64_t length) noexcept {
    return length > (UINT64_MAX - base);
}

[[nodiscard]] std::size_t encoded_size(std::size_t grant_count) noexcept {
    return static_cast<std::size_t>(device_access_header_bytes_v1) +
        (grant_count * static_cast<std::size_t>(mmio_grant_bytes_v1));
}

void write_header(
    os::core::MutableByteSpan output,
    ExecutionDomain domain,
    DmaCapability dma,
    std::size_t grant_count) noexcept {
    for (std::size_t index = 0U; index < device_access_magic.size(); ++index) {
        output[offset_magic + index] = device_access_magic[index];
    }
    write_u16_le(output, offset_version, device_access_version_v1);
    write_u16_le(output, offset_header_size, device_access_header_bytes_v1);
    write_u32_le(output, offset_total_size, static_cast<std::uint32_t>(encoded_size(grant_count)));
    output[offset_domain] = static_cast<std::byte>(domain);
    output[offset_dma] = static_cast<std::byte>(dma);
    write_u16_le(output, offset_grant_count, static_cast<std::uint16_t>(grant_count));
}

void write_grant(
    os::core::MutableByteSpan output,
    std::size_t record_offset,
    const MmioGrant& grant) noexcept {
    write_u64_le(output, record_offset + grant_offset_base, grant.base);
    write_u64_le(output, record_offset + grant_offset_length, grant.length);
    output[record_offset + grant_offset_access] = static_cast<std::byte>(grant.access);
}

[[nodiscard]] os::core::Result<std::size_t> encode_common(
    os::core::MutableByteSpan output,
    ExecutionDomain domain,
    DmaCapability dma,
    const std::array<MmioGrant, max_mmio_grants>& grants,
    std::size_t grant_count) noexcept {
    const auto total = encoded_size(grant_count);
    if (output.size() < total) {
        return os::core::Result<std::size_t>{device_error(errors::malformed_record)};
    }

    // Zero first so no reserved byte can carry whatever the caller's buffer
    // happened to hold.
    for (std::size_t index = 0U; index < total; ++index) {
        output[index] = std::byte{0};
    }

    write_header(output, domain, dma, grant_count);
    for (std::size_t index = 0U; index < grant_count; ++index) {
        write_grant(
            output,
            static_cast<std::size_t>(device_access_header_bytes_v1) +
                (index * static_cast<std::size_t>(mmio_grant_bytes_v1)),
            grants[index]);
    }
    return os::core::Result<std::size_t>{total};
}

} // namespace

bool DeviceAccessPolicyV1::permits(
    std::uint64_t base,
    std::uint64_t length,
    AccessMode mode) const noexcept {
    // A zero-length request is not a request for anything, and treating it as
    // permitted would make "is this range allowed" answer yes for a range that
    // was never checked against a window.
    if (length == 0U || grant_wraps(base, length)) {
        return false;
    }
    for (std::size_t index = 0U; index < grant_count_; ++index) {
        const auto& granted = grants_[index];
        if (mode == AccessMode::read_write && granted.access != AccessMode::read_write) {
            continue;
        }
        if (base < granted.base || length > granted.length) {
            continue;
        }
        // Both operands are already known non-negative in unsigned terms:
        // base >= granted.base and length <= granted.length. Written this way
        // rather than as base + length <= granted.base + granted.length, whose
        // right-hand side can wrap.
        if ((base - granted.base) <= (granted.length - length)) {
            return true;
        }
    }
    return false;
}

os::core::Result<DeviceAccessPolicyV1> parse_device_access_v1(os::core::ByteSpan encoded) {
    using ResultType = os::core::Result<DeviceAccessPolicyV1>;

    if (encoded.size() < static_cast<std::size_t>(device_access_header_bytes_v1) ||
        encoded.size() > max_device_access_bytes) {
        return ResultType{device_error(errors::malformed_record)};
    }
    for (std::size_t index = 0U; index < device_access_magic.size(); ++index) {
        if (encoded[offset_magic + index] != device_access_magic[index]) {
            return ResultType{device_error(errors::malformed_record)};
        }
    }
    if (read_u16_le(encoded, offset_version) != device_access_version_v1) {
        return ResultType{device_error(errors::unsupported_version)};
    }
    if (read_u16_le(encoded, offset_header_size) != device_access_header_bytes_v1) {
        return ResultType{device_error(errors::malformed_record)};
    }
    if (read_u32_le(encoded, offset_reserved0) != 0U ||
        read_u32_le(encoded, offset_reserved1) != 0U ||
        read_u64_le(encoded, offset_reserved2) != 0U) {
        return ResultType{device_error(errors::reserved_not_zero)};
    }

    const auto grant_count = static_cast<std::size_t>(read_u16_le(encoded, offset_grant_count));
    if (grant_count > max_mmio_grants) {
        return ResultType{device_error(errors::too_many_grants)};
    }

    const auto expected = encoded_size(grant_count);
    if (static_cast<std::uint64_t>(read_u32_le(encoded, offset_total_size)) !=
        static_cast<std::uint64_t>(expected)) {
        return ResultType{device_error(errors::malformed_record)};
    }
    if (encoded.size() != expected) {
        return ResultType{device_error(errors::trailing_bytes)};
    }

    const auto raw_domain = std::to_integer<std::uint8_t>(encoded[offset_domain]);
    if (!valid_domain(raw_domain)) {
        return ResultType{device_error(errors::unknown_domain)};
    }
    const auto raw_dma = std::to_integer<std::uint8_t>(encoded[offset_dma]);
    if (!valid_dma(raw_dma)) {
        return ResultType{device_error(errors::unknown_dma_capability)};
    }

    DeviceAccessPolicyV1 policy{};
    policy.domain_ = static_cast<ExecutionDomain>(raw_domain);
    policy.dma_ = static_cast<DmaCapability>(raw_dma);

    std::uint64_t previous_end = 0U;
    bool have_previous = false;
    for (std::size_t index = 0U; index < grant_count; ++index) {
        const auto record = static_cast<std::size_t>(device_access_header_bytes_v1) +
            (index * static_cast<std::size_t>(mmio_grant_bytes_v1));

        for (std::size_t byte = 0U; byte < grant_reserved_bytes; ++byte) {
            if (encoded[record + grant_offset_reserved + byte] != std::byte{0}) {
                return ResultType{device_error(errors::reserved_not_zero)};
            }
        }

        const auto raw_access = std::to_integer<std::uint8_t>(encoded[record + grant_offset_access]);
        if (!valid_access(raw_access)) {
            return ResultType{device_error(errors::unknown_access_mode)};
        }

        const auto base = read_u64_le(encoded, record + grant_offset_base);
        const auto length = read_u64_le(encoded, record + grant_offset_length);
        if (length == 0U) {
            return ResultType{device_error(errors::empty_grant)};
        }
        if (grant_wraps(base, length)) {
            return ResultType{device_error(errors::grant_overflow)};
        }
        // Canonical order: strictly ascending and non-overlapping. One
        // authority then has exactly one encoding, and an overlap cannot be
        // used to present the same registers twice under two different access
        // modes - which would make the effective permission depend on which
        // grant a checker happened to match first.
        if (have_previous && base < previous_end) {
            return ResultType{device_error(errors::grants_not_canonical)};
        }
        previous_end = base + length;
        have_previous = true;

        policy.grants_[index] = MmioGrant{base, length, static_cast<AccessMode>(raw_access)};
    }
    policy.grant_count_ = grant_count;

    // The claim the platform cannot back. Moving a driver out of the kernel
    // isolates it from the kernel's control flow, not from its memory: a device
    // that masters the bus with no IOMMU in front of it reaches everything
    // regardless of where its driver runs.
    if (policy.domain_ == ExecutionDomain::isolated_user &&
        policy.dma_ == DmaCapability::unconfined) {
        return ResultType{device_error(errors::unconfined_isolation)};
    }

    return ResultType{policy};
}

os::core::Result<std::size_t> encode_device_access_v1(
    const DeviceAccessPolicyV1& policy,
    os::core::MutableByteSpan output) {
    // Copied out through the public accessor rather than reaching into the
    // type. The encoder has no need to be a friend, and a narrower friend list
    // is a narrower way for an invariant to be bypassed later.
    std::array<MmioGrant, max_mmio_grants> grants{};
    for (std::size_t index = 0U; index < policy.grant_count(); ++index) {
        grants[index] = policy.grant(index);
    }
    return encode_common(output, policy.domain(), policy.dma(), grants, policy.grant_count());
}

os::core::Result<void> DeviceAccessPolicyBuilder::add_grant(
    std::uint64_t base,
    std::uint64_t length,
    AccessMode access) noexcept {
    if (grant_count_ >= max_mmio_grants) {
        return os::core::Result<void>{device_error(errors::too_many_grants)};
    }
    if (length == 0U) {
        return os::core::Result<void>{device_error(errors::empty_grant)};
    }
    if (grant_wraps(base, length)) {
        return os::core::Result<void>{device_error(errors::grant_overflow)};
    }
    // The parser requires canonical order, so the builder refuses to produce a
    // record the parser would reject. A producer should fail where the mistake
    // is, not hand back bytes that fail to decode somewhere else later.
    if (grant_count_ != 0U) {
        const auto& previous = grants_[grant_count_ - 1U];
        if (base < previous.base + previous.length) {
            return os::core::Result<void>{device_error(errors::grants_not_canonical)};
        }
    }
    grants_[grant_count_] = MmioGrant{base, length, access};
    ++grant_count_;
    return os::core::Result<void>{};
}

os::core::Result<std::size_t> DeviceAccessPolicyBuilder::encode(
    os::core::MutableByteSpan output) const noexcept {
    return encode_common(output, domain_, dma_, grants_, grant_count_);
}

} // namespace os::device
