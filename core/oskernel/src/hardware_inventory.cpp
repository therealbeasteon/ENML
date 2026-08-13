#include <os/kernel/hardware_inventory.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error inventory_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] bool read_be32(os::core::ByteSpan value, std::size_t offset, std::uint32_t& out) noexcept {
    if (offset > value.size() || value.size() - offset < 4U) return false;
    out =
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[offset])) << 24U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[offset + 1U])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[offset + 2U])) << 8U) |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[offset + 3U]));
    return true;
}

[[nodiscard]] bool read_cells(
    os::core::ByteSpan value,
    std::size_t offset,
    std::uint8_t cells,
    std::uint64_t& out) noexcept {
    if (cells == 0U || cells > 2U) return false;
    const std::size_t bytes = static_cast<std::size_t>(cells) * 4U;
    if (offset > value.size() || bytes > value.size() - offset) return false;
    out = 0ULL;
    for (std::uint8_t index = 0U; index < cells; ++index) {
        std::uint32_t word = 0U;
        if (!read_be32(value, offset + static_cast<std::size_t>(index) * 4U, word)) return false;
        out = (out << 32U) | static_cast<std::uint64_t>(word);
    }
    return true;
}

struct NodeState final {
    std::uint8_t child_address_cells {2U};
    std::uint8_t child_size_cells {1U};
    std::uint8_t parent_address_cells {2U};
    std::uint8_t parent_size_cells {1U};
    bool memory {false};
    bool reserved_memory_root {false};
    bool inside_reserved_memory {false};
    bool has_reg {false};
    HardwareRange first_reg {};
    std::array<char, DiscoveredDevice::max_compatible_bytes> compatible {};
    std::uint8_t compatible_size {0U};
};

struct DiscoveryContext final {
    static constexpr std::size_t max_depth = 16U;

    HardwareInventory inventory {};
    std::array<NodeState, max_depth> nodes {};
    os::core::Error error {};
    bool failed {false};
};

void fail(DiscoveryContext& context, std::uint32_t code) noexcept {
    if (!context.failed) {
        context.error = inventory_error(code);
        context.failed = true;
    }
}

bool add_reserved(DiscoveryContext& context, HardwareRange range) noexcept {
    if (!range.valid()) {
        fail(context, hardware_inventory_errors::malformed_reg);
        return false;
    }
    if (context.inventory.reserved_count >= context.inventory.reserved.size()) {
        fail(context, hardware_inventory_errors::inventory_exhausted);
        return false;
    }
    context.inventory.reserved[context.inventory.reserved_count++] = range;
    return true;
}

bool reservation(void* opaque, std::uint64_t address, std::uint64_t size) noexcept {
    auto& context = *static_cast<DiscoveryContext*>(opaque);
    return add_reserved(context, HardwareRange{address, size});
}

bool begin_node(void* opaque, std::string_view name, std::size_t depth) noexcept {
    auto& context = *static_cast<DiscoveryContext*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, hardware_inventory_errors::depth_exhausted);
        return false;
    }

    NodeState state{};
    if (depth != 0U) {
        const auto& parent = context.nodes[depth - 1U];
        state.parent_address_cells = parent.child_address_cells;
        state.parent_size_cells = parent.child_size_cells;
        state.child_address_cells = parent.child_address_cells;
        state.child_size_cells = parent.child_size_cells;
        state.inside_reserved_memory =
            parent.reserved_memory_root || parent.inside_reserved_memory;
    }
    if (depth == 1U && name == "reserved-memory") {
        state.reserved_memory_root = true;
    }
    context.nodes[depth] = state;
    return true;
}

bool property(
    void* opaque,
    std::string_view name,
    os::core::ByteSpan value,
    std::size_t depth) noexcept {
    auto& context = *static_cast<DiscoveryContext*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, hardware_inventory_errors::depth_exhausted);
        return false;
    }
    auto& node = context.nodes[depth];

    if (name == "#address-cells" || name == "#size-cells") {
        if (value.size() != 4U) {
            fail(context, hardware_inventory_errors::invalid_property);
            return false;
        }
        // Zero is legal and common, not malformed. A bus whose children have
        // no address or no size declares it that way, and every ARM device
        // tree does: /cpus carries #size-cells = <0> because a CPU's reg is an
        // identifier rather than a range. Rejecting zero here failed the walk
        // on conformant input before it reached the memory nodes.
        std::uint32_t raw = 0U;
        if (!read_be32(value, 0U, raw) || raw > 2U) {
            fail(context, hardware_inventory_errors::unsupported_cells);
            return false;
        }
        if (name == "#address-cells") node.child_address_cells = static_cast<std::uint8_t>(raw);
        else node.child_size_cells = static_cast<std::uint8_t>(raw);
        return true;
    }

    if (name == "device_type") {
        constexpr std::string_view memory_name{"memory"};
        if (value.size() >= memory_name.size() + 1U) {
            bool equal = true;
            for (std::size_t i = 0U; i < memory_name.size(); ++i) {
                if (std::to_integer<char>(value[i]) != memory_name[i]) {
                    equal = false;
                    break;
                }
            }
            if (equal && value[memory_name.size()] == std::byte{0}) node.memory = true;
        }
        return true;
    }

    if (name == "compatible") {
        std::size_t length = 0U;
        while (length < value.size() && value[length] != std::byte{0}) ++length;
        if (length == 0U || length == value.size() ||
            length >= DiscoveredDevice::max_compatible_bytes) {
            fail(context, hardware_inventory_errors::invalid_property);
            return false;
        }
        for (std::size_t i = 0U; i < length; ++i) {
            node.compatible[i] = std::to_integer<char>(value[i]);
        }
        node.compatible_size = static_cast<std::uint8_t>(length);
        return true;
    }

    if (name == "reg") {
        if (node.parent_address_cells > 2U || node.parent_size_cells > 2U) {
            fail(context, hardware_inventory_errors::unsupported_cells);
            return false;
        }
        // A reg under a parent declaring no address or no size cells does not
        // describe a hardware range - /cpus/cpu@0 has reg = <0> under
        // #size-cells = <0>, and that zero is an identifier. Leave has_reg
        // false so the node is neither recorded as memory nor as a device, and
        // keep walking. Treating it as an error rejected trees that are
        // correct.
        if (node.parent_address_cells == 0U || node.parent_size_cells == 0U) {
            return true;
        }
        const std::size_t range_bytes =
            static_cast<std::size_t>(node.parent_address_cells + node.parent_size_cells) * 4U;
        if (range_bytes == 0U || value.size() < range_bytes || value.size() % range_bytes != 0U) {
            fail(context, hardware_inventory_errors::malformed_reg);
            return false;
        }
        std::uint64_t base = 0ULL;
        std::uint64_t size = 0ULL;
        if (!read_cells(value, 0U, node.parent_address_cells, base) ||
            !read_cells(value, static_cast<std::size_t>(node.parent_address_cells) * 4U,
                node.parent_size_cells, size)) {
            fail(context, hardware_inventory_errors::malformed_reg);
            return false;
        }
        const HardwareRange range{base, size};
        if (!range.valid()) {
            fail(context, hardware_inventory_errors::malformed_reg);
            return false;
        }
        node.first_reg = range;
        node.has_reg = true;
        return true;
    }

    return true;
}

bool end_node(void* opaque, std::size_t depth) noexcept {
    auto& context = *static_cast<DiscoveryContext*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, hardware_inventory_errors::depth_exhausted);
        return false;
    }
    const auto& node = context.nodes[depth];

    if (node.inside_reserved_memory && node.has_reg) {
        if (!add_reserved(context, node.first_reg)) return false;
    }

    if (node.memory && node.has_reg && !node.inside_reserved_memory) {
        if (context.inventory.memory_count >= context.inventory.memory.size()) {
            fail(context, hardware_inventory_errors::inventory_exhausted);
            return false;
        }
        context.inventory.memory[context.inventory.memory_count++] = node.first_reg;
    }

    if (node.compatible_size != 0U && node.has_reg && !node.memory &&
        !node.inside_reserved_memory && !node.reserved_memory_root) {
        if (context.inventory.device_count >= context.inventory.devices.size()) {
            // Record the truncation and keep walking. A full device table is a
            // capacity limit on an informational list, not a failure of the
            // walk, and aborting here abandoned the memory and reservation
            // ranges that had not been visited yet - so a board with more
            // devices than the table holds reported "no RAM". QEMU virt alone
            // publishes thirty-two virtio-mmio nodes before its other
            // peripherals, which is the whole table.
            //
            // Not silent: devices_truncated is part of the inventory, so a
            // caller that needs a device it cannot find can tell the
            // difference between absent and dropped.
            context.inventory.devices_truncated = true;
            return true;
        }
        auto& device = context.inventory.devices[context.inventory.device_count++];
        device.compatible = node.compatible;
        device.compatible_size = node.compatible_size;
        device.registers = node.first_reg;
    }

    return true;
}

} // namespace

os::core::Result<HardwareInventory>
discover_hardware(const FdtView& fdt) noexcept {
    DiscoveryContext context{};

    auto reservations = fdt.walk_reservations({&context, reservation});
    if (context.failed) return context.error;
    if (!reservations) return reservations.error();

    const FdtCallbacks callbacks{&context, begin_node, property, end_node};
    auto walked = fdt.walk(callbacks);
    if (context.failed) return context.error;
    if (!walked) return walked.error();
    return context.inventory;
}

} // namespace os::kernel
