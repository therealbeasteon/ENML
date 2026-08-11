#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>

// Device access policy: the authority a device-facing component is granted.
//
// M6.0 answers the question a hardware-neutral OS cannot avoid - how does code
// that must touch hardware get the authority to do so, without that authority
// becoming a way around every boundary above it?
//
// The shape comes from two observations in the references, and one place where
// ENML has to diverge from them.
//
// First, drivers are where the bugs are. Driver code has been measured at
// several times the defect density of other kernel code, and the large majority
// of crashes on at least one commodity OS were attributed to drivers. So driver
// code is the last code that should hold unrestricted authority.
//
// Second, the useful split is by *performance and priority*, not by
// device-dependent versus device-independent. Only about a third of driver
// functions are on the data path or run at high priority; initialization,
// configuration and error handling - roughly two thirds of the code, and the
// large majority of the churn - are not, and can run outside the kernel without
// measurable cost. Existing device-dependent/independent splits are the wrong
// seam because those halves talk constantly and move large structures.
//
// Where ENML diverges: the microdriver work grants the user-mode half access to
// I/O ports and maps the device's registers into it, because its goal is fault
// isolation - keeping a driver bug from panicking the kernel. That is a
// reasonable answer to that question and the wrong answer to ours. A component
// that can reach arbitrary I/O ports is not confined by anything; it is a
// kernel with extra steps. ENML's boundary is a security boundary, so:
//
//   - Port I/O authority is not representable. There is no field for it. This
//     is not an omission and must not be added: a bounded MMIO grant can be
//     checked, and "the I/O port space" cannot.
//
//   - MMIO is an explicit allow-list of bounded regions, never a mapping of
//     "the device's registers" whose extent nobody wrote down.
//
//   - DMA is treated as what it is. A device that can master the bus without an
//     IOMMU in front of it can read and write all of physical memory, which
//     includes the memory of everything that was supposed to be isolated from
//     it. On such a platform a driver is not confined by moving it out of the
//     kernel, and this format refuses to say otherwise.
//
// The last rule is the same principle as the boot-state capability set: a
// hardware-neutral system must represent the assurance the platform actually
// provides, not the assurance it would like to have. Running a driver in the
// kernel on an IOMMU-less platform is a legitimate, honest configuration.
// Running it outside the kernel and calling it isolated is not.
namespace os::device {

inline constexpr std::array<std::byte, 4> device_access_magic {
    std::byte{'E'}, std::byte{'D'}, std::byte{'A'}, std::byte{'1'}
};

inline constexpr std::uint16_t device_access_version_v1 = 1U;
inline constexpr std::uint16_t device_access_header_bytes_v1 = 32U;
inline constexpr std::uint16_t mmio_grant_bytes_v1 = 24U;

// A component needing more separate register windows than this is a component
// whose authority nobody has actually enumerated. The ceiling is a rejection
// criterion, not a buffer size.
inline constexpr std::size_t max_mmio_grants = 4U;

inline constexpr std::size_t max_device_access_bytes =
    static_cast<std::size_t>(device_access_header_bytes_v1) +
    (max_mmio_grants * static_cast<std::size_t>(mmio_grant_bytes_v1));

namespace errors {
inline constexpr std::uint32_t malformed_record = 1U;
inline constexpr std::uint32_t unsupported_version = 2U;
inline constexpr std::uint32_t reserved_not_zero = 3U;
inline constexpr std::uint32_t unknown_domain = 4U;
inline constexpr std::uint32_t unknown_dma_capability = 5U;
inline constexpr std::uint32_t unknown_access_mode = 6U;
inline constexpr std::uint32_t too_many_grants = 7U;
inline constexpr std::uint32_t empty_grant = 8U;
inline constexpr std::uint32_t grant_overflow = 9U;
inline constexpr std::uint32_t grants_not_canonical = 10U;
inline constexpr std::uint32_t trailing_bytes = 11U;
// The isolation claim the platform cannot back: an out-of-kernel component
// whose device can master the bus without an IOMMU.
inline constexpr std::uint32_t unconfined_isolation = 12U;
} // namespace errors

// Zero is never a valid discriminant, here as everywhere else in the tree. An
// all-zero buffer must not decode to a meaningful policy.
enum class ExecutionDomain : std::uint8_t {
    // On the data path or at high priority: stays in the kernel because moving
    // it costs more than the isolation is worth. Explicitly *not* confined.
    kernel_resident = 1U,
    // Initialization, configuration, error handling and everything else off the
    // critical path. Runs as an ordinary confined ENML service.
    isolated_user = 2U,
};

enum class DmaCapability : std::uint8_t {
    // The device cannot master the bus. Nothing to confine.
    none = 1U,
    // The device masters the bus through an IOMMU, so its reach is whatever the
    // IOMMU was programmed to allow.
    iommu_confined = 2U,
    // The device masters the bus directly. Its reach is all of physical memory,
    // whatever the driver's execution domain happens to be.
    unconfined = 3U,
};

enum class AccessMode : std::uint8_t {
    read_only = 1U,
    read_write = 2U,
};

// One bounded register window. Both ends are explicit; there is deliberately no
// way to express "this device's registers" without saying where they end.
struct MmioGrant final {
    std::uint64_t base {0};
    std::uint64_t length {0};
    AccessMode access {AccessMode::read_only};

    [[nodiscard]] friend bool operator==(const MmioGrant&, const MmioGrant&) = default;
};

// The default policy grants nothing.
//
// This is the counterpart of the boot state defaulting to unverified. A caller
// that forgets to parse, or parses and ignores the error, holds a policy that
// is isolated, cannot DMA, and can reach no registers at all. Authority is
// something a record has to actively confer.
class DeviceAccessPolicyV1 final {
public:
    DeviceAccessPolicyV1() noexcept = default;

    [[nodiscard]] ExecutionDomain domain() const noexcept { return domain_; }
    [[nodiscard]] DmaCapability dma() const noexcept { return dma_; }
    [[nodiscard]] std::size_t grant_count() const noexcept { return grant_count_; }

    [[nodiscard]] const MmioGrant& grant(std::size_t index) const noexcept {
        return grants_[index];
    }

    // Whether this component is actually confined, as opposed to merely running
    // outside the kernel. Callers deciding what to trust must ask this rather
    // than testing the execution domain, which on its own says only where the
    // code runs and nothing about what it can reach.
    [[nodiscard]] bool confined() const noexcept {
        return domain_ == ExecutionDomain::isolated_user && dma_ != DmaCapability::unconfined;
    }

    // Whether an address range falls entirely inside a single granted window.
    // Ranges spanning two adjacent grants are refused: adjacency is an accident
    // of layout, not a statement that the two windows are one object.
    [[nodiscard]] bool permits(std::uint64_t base, std::uint64_t length, AccessMode mode) const noexcept;

private:
    // Only the parser is a friend, and only because it is the one path that
    // may populate a policy. The encoder reads through the public accessors.
    friend os::core::Result<DeviceAccessPolicyV1> parse_device_access_v1(os::core::ByteSpan);

    ExecutionDomain domain_ {ExecutionDomain::isolated_user};
    DmaCapability dma_ {DmaCapability::none};
    std::size_t grant_count_ {0};
    std::array<MmioGrant, max_mmio_grants> grants_ {};
};

// Decodes and validates a policy record.
//
// Beyond the structural checks the rest of the tree already applies - exact
// magic, exact version, exact header size, declared length matching real
// length, reserved fields zero, unknown discriminants rejected - this enforces:
//
//   - grants are canonical: strictly ascending by base and non-overlapping, so
//     one authority has exactly one encoding and an overlap cannot be used to
//     smuggle a second, differently-permissioned view of the same registers;
//   - no grant is empty, and no grant's base plus length wraps;
//   - an isolated component's device must not be able to master the bus without
//     an IOMMU, because that is an isolation claim the platform cannot back.
[[nodiscard]] os::core::Result<DeviceAccessPolicyV1> parse_device_access_v1(os::core::ByteSpan encoded);

[[nodiscard]] os::core::Result<std::size_t> encode_device_access_v1(
    const DeviceAccessPolicyV1& policy,
    os::core::MutableByteSpan output);

// Assembles a policy for encoding. Deliberately has no build() returning a
// policy: a usable policy comes from parsing a record, so the validation above
// is on the only path into the type.
class DeviceAccessPolicyBuilder final {
public:
    DeviceAccessPolicyBuilder() noexcept = default;

    void set_domain(ExecutionDomain domain) noexcept { domain_ = domain; }
    void set_dma(DmaCapability dma) noexcept { dma_ = dma; }

    [[nodiscard]] os::core::Result<void> add_grant(
        std::uint64_t base,
        std::uint64_t length,
        AccessMode access) noexcept;

    [[nodiscard]] os::core::Result<std::size_t> encode(os::core::MutableByteSpan output) const noexcept;

private:
    ExecutionDomain domain_ {ExecutionDomain::isolated_user};
    DmaCapability dma_ {DmaCapability::none};
    std::size_t grant_count_ {0};
    std::array<MmioGrant, max_mmio_grants> grants_ {};
};

} // namespace os::device
