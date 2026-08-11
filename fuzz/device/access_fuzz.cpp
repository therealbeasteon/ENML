#include <array>
#include <cstddef>
#include <cstdint>

#include <os/device/access.hpp>

// The device access policy decoder.
//
// A policy record decides how much of physical address space a device-facing
// component may reach. Anything the parser accepts becomes authority, so the
// invariants asserted here are the ones the rest of the system would otherwise
// have to re-derive at every use: grants are bounded, canonical and
// non-overlapping, and an isolation claim is never accepted from a platform
// that cannot back it.

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    os::core::ByteSpan input{reinterpret_cast<const std::byte*>(data), size};
    auto parsed = os::device::parse_device_access_v1(input);
    if (!parsed) {
        return 0;
    }
    const auto& policy = parsed.value();

    if (policy.grant_count() > os::device::max_mmio_grants) {
        __builtin_trap();
    }

    // An out-of-kernel component whose device masters the bus with no IOMMU is
    // not confined by anything, and must never have parsed.
    if (policy.domain() == os::device::ExecutionDomain::isolated_user &&
        policy.dma() == os::device::DmaCapability::unconfined) {
        __builtin_trap();
    }
    // confined() must agree with the fields it is derived from, since callers
    // are told to ask it rather than test the domain themselves.
    const bool expected_confined =
        policy.domain() == os::device::ExecutionDomain::isolated_user &&
        policy.dma() != os::device::DmaCapability::unconfined;
    if (policy.confined() != expected_confined) {
        __builtin_trap();
    }

    std::uint64_t previous_end = 0U;
    for (std::size_t index = 0U; index < policy.grant_count(); ++index) {
        const auto& grant = policy.grant(index);

        // Empty and wrapping windows describe nothing, and every containment
        // check would be meaningless for them.
        if (grant.length == 0U || grant.length > (UINT64_MAX - grant.base)) {
            __builtin_trap();
        }
        // Canonical: strictly ascending and non-overlapping.
        if (index != 0U && grant.base < previous_end) {
            __builtin_trap();
        }
        previous_end = grant.base + grant.length;

        // Whatever was granted must be permitted, and the byte on each side of
        // it must not be. This is the property attacker-chosen bytes would have
        // to break to widen a window.
        if (!policy.permits(grant.base, grant.length, os::device::AccessMode::read_only)) {
            __builtin_trap();
        }
        if (grant.length < UINT64_MAX - grant.base &&
            policy.permits(grant.base, grant.length + 1U, os::device::AccessMode::read_only)) {
            __builtin_trap();
        }
        // A range straddling the window's lower edge must be refused. No single
        // grant can contain it: canonical order puts every earlier window's end
        // at or below this base, so an earlier window cannot reach past it, and
        // this window starts above the first byte of the range. Adjacent
        // windows are still two windows, not one.
        if (grant.base != 0U && policy.permits(grant.base - 1U, 2U, os::device::AccessMode::read_only)) {
            __builtin_trap();
        }
        // A read-only window must never satisfy a write.
        if (grant.access == os::device::AccessMode::read_only &&
            policy.permits(grant.base, grant.length, os::device::AccessMode::read_write)) {
            __builtin_trap();
        }
    }

    // Re-encoding an accepted policy must reproduce it exactly. A record that
    // decodes to one authority and re-encodes to another would let the stored
    // form and the enforced form drift apart.
    std::array<std::byte, os::device::max_device_access_bytes> buffer{};
    auto encoded = os::device::encode_device_access_v1(policy, buffer);
    if (!encoded || encoded.value() != size) {
        __builtin_trap();
    }
    for (std::size_t index = 0U; index < size; ++index) {
        if (buffer[index] != input[index]) {
            __builtin_trap();
        }
    }

    return 0;
}
