#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/fdt.hpp>
#include <os/kernel/hardware_inventory.hpp>

namespace os::kernel {

namespace gic_v3_discovery_errors {
inline constexpr std::uint32_t not_found = 90U;
inline constexpr std::uint32_t ambiguous = 91U;
inline constexpr std::uint32_t malformed = 92U;
inline constexpr std::uint32_t unsupported_cells = 93U;
inline constexpr std::uint32_t too_many_redistributor_regions = 94U;
inline constexpr std::uint32_t inconsistent_region_count = 95U;
} // namespace gic_v3_discovery_errors

// Early Cookie only needs enough topology to establish interrupt delivery. ITS,
// MSI, affinity partitions and optional legacy CPU interfaces remain outside
// this object and will be discovered by their owning services/drivers later.
struct GicV3Discovery final {
    static constexpr std::size_t max_redistributor_regions = 8U;

    HardwareRange distributor {};
    std::array<HardwareRange, max_redistributor_regions> redistributors {};
    std::size_t redistributor_count {0U};
    std::uint64_t redistributor_stride {0ULL};
    std::uint32_t declared_redistributor_regions {0U};

    [[nodiscard]] bool valid() const noexcept {
        if (!distributor.valid() || redistributor_count == 0U ||
            redistributor_count > redistributors.size()) return false;
        for (std::size_t i = 0U; i < redistributor_count; ++i) {
            if (!redistributors[i].valid()) return false;
        }
        if (declared_redistributor_regions != 0U &&
            declared_redistributor_regions != redistributor_count) return false;
        if (redistributor_stride != 0ULL &&
            (redistributor_stride % 0x10000ULL) != 0ULL) return false;
        return true;
    }
};

// Finds exactly one `arm,gic-v3` compatible interrupt controller and decodes
// the ordered DT `reg` tuples: GICD first, followed by one or more GICR regions.
// The generic hardware inventory intentionally stores only one MMIO range per
// device, so security-critical interrupt topology gets this dedicated parser.
[[nodiscard]] os::core::Result<GicV3Discovery>
discover_gic_v3(const FdtView& fdt) noexcept;

} // namespace os::kernel
