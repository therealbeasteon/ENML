#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>
#include <os/kernel/fdt.hpp>

namespace os::kernel {

namespace hardware_inventory_errors {
inline constexpr std::uint32_t unsupported_cells = 70U;
inline constexpr std::uint32_t malformed_reg = 71U;
inline constexpr std::uint32_t depth_exhausted = 72U;
inline constexpr std::uint32_t inventory_exhausted = 73U;
inline constexpr std::uint32_t invalid_property = 74U;
} // namespace hardware_inventory_errors

struct HardwareRange final {
    std::uint64_t base {0ULL};
    std::uint64_t size {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return size != 0ULL && base <= UINT64_MAX - (size - 1ULL);
    }

    [[nodiscard]] constexpr std::uint64_t end_exclusive() const noexcept {
        return valid() ? base + size : 0ULL;
    }
};

struct DiscoveredDevice final {
    static constexpr std::size_t max_compatible_bytes = 64U;

    std::array<char, max_compatible_bytes> compatible {};
    std::uint8_t compatible_size {0U};
    HardwareRange registers {};

    [[nodiscard]] std::string_view compatible_view() const noexcept {
        return std::string_view(compatible.data(), compatible_size);
    }
    [[nodiscard]] bool valid() const noexcept {
        return compatible_size != 0U && registers.valid();
    }
};

struct HardwareInventory final {
    static constexpr std::size_t max_memory_regions = 8U;
    static constexpr std::size_t max_reserved_regions = 16U;
    static constexpr std::size_t max_devices = 32U;

    std::array<HardwareRange, max_memory_regions> memory {};
    std::size_t memory_count {0U};
    // Authoritative FDT memory-reservation block plus later static
    // /reserved-memory ranges. Boot allocators must subtract these from RAM.
    std::array<HardwareRange, max_reserved_regions> reserved {};
    std::size_t reserved_count {0U};
    std::array<DiscoveredDevice, max_devices> devices {};
    std::size_t device_count {0U};
};

// Builds a small early-boot inventory from a validated FDT. It intentionally
// records RAM, reserved physical ranges, and the first register range of
// compatible MMIO devices. IRQ topology, clocks and device-specific properties
// remain owned by future drivers; boot discovery must not become a second
// driver stack.
[[nodiscard]] os::core::Result<HardwareInventory>
discover_hardware(const FdtView& fdt) noexcept;

} // namespace os::kernel
