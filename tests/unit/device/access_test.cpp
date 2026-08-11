#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/device/access.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "device access: %s\n", what);
    }
    return condition;
}

using Buffer = std::array<std::byte, os::device::max_device_access_bytes>;

// Byte offsets, duplicated from the encoder on purpose. A test that derives
// offsets from the code under test cannot catch the code moving a field.
constexpr std::size_t offset_domain = 12U;
constexpr std::size_t offset_dma = 13U;
constexpr std::size_t offset_grant_count = 14U;
constexpr std::size_t header_bytes = 32U;
constexpr std::size_t grant_bytes = 24U;
constexpr std::size_t grant_offset_base = 0U;
constexpr std::size_t grant_offset_length = 8U;
constexpr std::size_t grant_offset_access = 16U;

void write_u64(Buffer& buffer, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        buffer[offset + index] =
            static_cast<std::byte>((value >> static_cast<unsigned>(index * 8U)) & 0xFFU);
    }
}

} // namespace

int main() {
    Buffer buffer{};

    // A default policy grants nothing. This is the property everything else
    // rests on: forgetting to parse must not confer authority.
    {
        const os::device::DeviceAccessPolicyV1 defaulted{};
        if (!check(defaulted.grant_count() == 0U, "default policy had grants")) return 1;
        if (!check(defaulted.dma() == os::device::DmaCapability::none,
                   "default policy could DMA")) return 1;
        if (!check(defaulted.confined(), "default policy was not confined")) return 1;
        if (!check(!defaulted.permits(0U, 1U, os::device::AccessMode::read_only),
                   "default policy permitted a read")) return 1;
    }

    // Round trip: an isolated, IOMMU-confined component with two windows.
    {
        os::device::DeviceAccessPolicyBuilder builder;
        builder.set_domain(os::device::ExecutionDomain::isolated_user);
        builder.set_dma(os::device::DmaCapability::iommu_confined);
        if (!check(static_cast<bool>(builder.add_grant(
                0x1000U, 0x1000U, os::device::AccessMode::read_write)), "grant rejected")) return 1;
        if (!check(static_cast<bool>(builder.add_grant(
                0x8000U, 0x100U, os::device::AccessMode::read_only)), "grant rejected")) return 1;

        auto encoded = builder.encode(buffer);
        if (!check(static_cast<bool>(encoded), "encode failed")) return 1;
        if (!check(encoded.value() == header_bytes + (2U * grant_bytes),
                   "unexpected encoded size")) return 1;

        auto parsed = os::device::parse_device_access_v1({buffer.data(), encoded.value()});
        if (!check(static_cast<bool>(parsed), "round trip rejected")) return 1;
        const auto& policy = parsed.value();
        if (!check(policy.grant_count() == 2U, "round trip lost grants")) return 1;
        if (!check(policy.confined(), "iommu-confined isolated policy reported unconfined")) return 1;

        // Containment, at both edges and just past them.
        if (!check(policy.permits(0x1000U, 0x1000U, os::device::AccessMode::read_write),
                   "exact window denied")) return 1;
        if (!check(policy.permits(0x1FFFU, 1U, os::device::AccessMode::read_write),
                   "last byte denied")) return 1;
        if (!check(!policy.permits(0x1000U, 0x1001U, os::device::AccessMode::read_write),
                   "one byte past the window allowed")) return 1;
        if (!check(!policy.permits(0xFFFU, 2U, os::device::AccessMode::read_only),
                   "range starting below the window allowed")) return 1;
        if (!check(!policy.permits(0x2000U, 1U, os::device::AccessMode::read_only),
                   "range past the window allowed")) return 1;

        // A read-only window must not satisfy a write.
        if (!check(policy.permits(0x8000U, 0x100U, os::device::AccessMode::read_only),
                   "read denied on read-only window")) return 1;
        if (!check(!policy.permits(0x8000U, 0x100U, os::device::AccessMode::read_write),
                   "write allowed on a read-only window")) return 1;

        // A zero-length request is not a request, and must never answer yes.
        if (!check(!policy.permits(0x1000U, 0U, os::device::AccessMode::read_only),
                   "zero-length range permitted")) return 1;

        // A request whose end wraps must be refused rather than wrapping into
        // a window at the bottom of the address space.
        if (!check(!policy.permits(UINT64_MAX, 2U, os::device::AccessMode::read_only),
                   "wrapping range permitted")) return 1;
    }

    // The central rule: an out-of-kernel component whose device masters the bus
    // with no IOMMU is not isolated, whatever the record says.
    {
        os::device::DeviceAccessPolicyBuilder builder;
        builder.set_domain(os::device::ExecutionDomain::isolated_user);
        builder.set_dma(os::device::DmaCapability::unconfined);
        auto encoded = builder.encode(buffer);
        if (!check(static_cast<bool>(encoded), "encode failed")) return 1;
        if (!check(!os::device::parse_device_access_v1({buffer.data(), encoded.value()}),
                   "isolated component accepted with unconfined DMA")) return 1;
    }

    // The same device in the kernel is honest, and must be accepted. Degraded
    // platforms have to remain expressible or the format forces a lie.
    {
        os::device::DeviceAccessPolicyBuilder builder;
        builder.set_domain(os::device::ExecutionDomain::kernel_resident);
        builder.set_dma(os::device::DmaCapability::unconfined);
        auto encoded = builder.encode(buffer);
        if (!check(static_cast<bool>(encoded), "encode failed")) return 1;
        auto parsed = os::device::parse_device_access_v1({buffer.data(), encoded.value()});
        if (!check(static_cast<bool>(parsed), "kernel-resident unconfined policy rejected")) return 1;
        // It is accepted, and it is not confined. Both matter.
        if (!check(!parsed.value().confined(),
                   "kernel-resident policy reported itself confined")) return 1;
    }

    // The builder refuses to produce a record its own parser would reject.
    {
        os::device::DeviceAccessPolicyBuilder builder;
        if (!check(static_cast<bool>(builder.add_grant(
                0x2000U, 0x1000U, os::device::AccessMode::read_only)), "grant rejected")) return 1;
        if (!check(!builder.add_grant(0x1000U, 0x1000U, os::device::AccessMode::read_only),
                   "descending grant accepted")) return 1;
        if (!check(!builder.add_grant(0x2800U, 0x100U, os::device::AccessMode::read_only),
                   "overlapping grant accepted")) return 1;
        if (!check(!builder.add_grant(0x9000U, 0U, os::device::AccessMode::read_only),
                   "empty grant accepted")) return 1;
        if (!check(!builder.add_grant(UINT64_MAX, 2U, os::device::AccessMode::read_only),
                   "wrapping grant accepted")) return 1;
    }

    // Overlapping grants crafted directly in bytes must be rejected. Two views
    // of the same registers under different access modes would make the
    // effective permission depend on match order.
    {
        os::device::DeviceAccessPolicyBuilder builder;
        if (!check(static_cast<bool>(builder.add_grant(
                0x1000U, 0x1000U, os::device::AccessMode::read_only)), "grant rejected")) return 1;
        if (!check(static_cast<bool>(builder.add_grant(
                0x4000U, 0x1000U, os::device::AccessMode::read_write)), "grant rejected")) return 1;
        auto encoded = builder.encode(buffer);
        if (!check(static_cast<bool>(encoded), "encode failed")) return 1;

        // Move the second window back on top of the first.
        write_u64(buffer, header_bytes + grant_bytes + grant_offset_base, 0x1800U);
        if (!check(!os::device::parse_device_access_v1({buffer.data(), encoded.value()}),
                   "overlapping grants accepted")) return 1;

        // Descending order, without overlap, is also non-canonical.
        write_u64(buffer, header_bytes + grant_bytes + grant_offset_base, 0x0100U);
        if (!check(!os::device::parse_device_access_v1({buffer.data(), encoded.value()}),
                   "descending grants accepted")) return 1;
    }

    // Unknown discriminants are rejected rather than defaulted, and a length of
    // zero is rejected even though it encodes cleanly.
    {
        os::device::DeviceAccessPolicyBuilder builder;
        builder.set_domain(os::device::ExecutionDomain::kernel_resident);
        builder.set_dma(os::device::DmaCapability::none);
        if (!check(static_cast<bool>(builder.add_grant(
                0x1000U, 0x1000U, os::device::AccessMode::read_write)), "grant rejected")) return 1;
        auto encoded = builder.encode(buffer);
        if (!check(static_cast<bool>(encoded), "encode failed")) return 1;
        const auto size = encoded.value();
        if (!check(static_cast<bool>(os::device::parse_device_access_v1({buffer.data(), size})),
                   "baseline rejected")) return 1;

        const std::size_t poisoned[] {
            offset_domain,
            offset_dma,
            header_bytes + grant_offset_access,
        };
        for (const auto offset : poisoned) {
            const auto original = buffer[offset];
            buffer[offset] = std::byte{0};
            if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                       "zero discriminant accepted")) return 1;
            buffer[offset] = std::byte{0x7F};
            if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                       "unknown discriminant accepted")) return 1;
            buffer[offset] = original;
        }

        write_u64(buffer, header_bytes + grant_offset_length, 0U);
        if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                   "empty grant accepted")) return 1;
        write_u64(buffer, header_bytes + grant_offset_length, 0x1000U);

        // A grant whose end wraps describes a window that does not exist.
        write_u64(buffer, header_bytes + grant_offset_base, UINT64_MAX);
        if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                   "wrapping grant accepted")) return 1;
        write_u64(buffer, header_bytes + grant_offset_base, 0x1000U);

        // Declared grant count above the ceiling, and trailing bytes.
        buffer[offset_grant_count] = static_cast<std::byte>(os::device::max_mmio_grants + 1U);
        if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                   "grant count above the ceiling accepted")) return 1;
        buffer[offset_grant_count] = std::byte{1};

        if (!check(!os::device::parse_device_access_v1({buffer.data(), size - 1U}),
                   "truncated record accepted")) return 1;
        if (!check(!os::device::parse_device_access_v1({buffer.data(), size + 1U}),
                   "trailing bytes accepted")) return 1;

        // Every structural byte of the header must matter.
        for (std::size_t offset = 0U; offset < 12U; ++offset) {
            const auto original = buffer[offset];
            buffer[offset] = static_cast<std::byte>(std::to_integer<std::uint8_t>(original) ^ 0xFFU);
            if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                       "corrupted header byte accepted")) return 1;
            buffer[offset] = original;
        }

        // Reserved header bytes must be zero.
        for (std::size_t offset = 16U; offset < header_bytes; ++offset) {
            const auto original = buffer[offset];
            buffer[offset] = std::byte{1};
            if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                       "nonzero reserved header byte accepted")) return 1;
            buffer[offset] = original;
        }

        // As must the reserved bytes of a grant record.
        for (std::size_t offset = 17U; offset < grant_bytes; ++offset) {
            const auto original = buffer[header_bytes + offset];
            buffer[header_bytes + offset] = std::byte{1};
            if (!check(!os::device::parse_device_access_v1({buffer.data(), size}),
                       "nonzero reserved grant byte accepted")) return 1;
            buffer[header_bytes + offset] = original;
        }
    }

    return 0;
}
