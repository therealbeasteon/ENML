#include <os/kernel/gic_v3_discovery.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error gic_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] bool read_be32(
    os::core::ByteSpan value,
    std::size_t offset,
    std::uint32_t& out) noexcept {
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
    if (offset > value.size() || value.size() - offset < bytes) return false;
    out = 0ULL;
    for (std::uint8_t index = 0U; index < cells; ++index) {
        std::uint32_t word = 0U;
        if (!read_be32(value, offset + static_cast<std::size_t>(index) * 4U, word)) return false;
        out = (out << 32U) | word;
    }
    return true;
}

[[nodiscard]] bool compatible_contains(
    os::core::ByteSpan value,
    std::string_view wanted) noexcept {
    std::size_t offset = 0U;
    while (offset < value.size()) {
        std::size_t end = offset;
        while (end < value.size() && value[end] != std::byte{0}) ++end;
        if (end == value.size()) return false;
        if (end - offset == wanted.size()) {
            bool same = true;
            for (std::size_t i = 0U; i < wanted.size(); ++i) {
                if (std::to_integer<char>(value[offset + i]) != wanted[i]) {
                    same = false;
                    break;
                }
            }
            if (same) return true;
        }
        offset = end + 1U;
    }
    return false;
}

struct NodeState final {
    std::uint8_t child_address_cells {2U};
    std::uint8_t child_size_cells {1U};
    std::uint8_t parent_address_cells {2U};
    std::uint8_t parent_size_cells {1U};
    bool gic_v3 {false};
    bool interrupt_controller {false};
    bool has_reg {false};
    os::core::ByteSpan reg {};
    std::uint64_t redistributor_stride {0ULL};
    std::uint32_t declared_redistributor_regions {0U};
};

struct Context final {
    static constexpr std::size_t max_depth = 16U;

    std::array<NodeState, max_depth> nodes {};
    GicV3Discovery result {};
    std::size_t match_count {0U};
    os::core::Error error {};
    bool failed {false};
};

void fail(Context& context, std::uint32_t code) noexcept {
    if (!context.failed) {
        context.error = gic_error(code);
        context.failed = true;
    }
}

bool begin_node(void* opaque, std::string_view, std::size_t depth) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, gic_v3_discovery_errors::malformed);
        return false;
    }
    NodeState state{};
    if (depth != 0U) {
        const auto& parent = context.nodes[depth - 1U];
        state.parent_address_cells = parent.child_address_cells;
        state.parent_size_cells = parent.child_size_cells;
        state.child_address_cells = parent.child_address_cells;
        state.child_size_cells = parent.child_size_cells;
    }
    context.nodes[depth] = state;
    return true;
}

bool property(
    void* opaque,
    std::string_view name,
    os::core::ByteSpan value,
    std::size_t depth) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, gic_v3_discovery_errors::malformed);
        return false;
    }
    auto& node = context.nodes[depth];

    if (name == "#address-cells" || name == "#size-cells") {
        std::uint32_t raw = 0U;
        if (value.size() != 4U || !read_be32(value, 0U, raw) || raw == 0U || raw > 2U) {
            fail(context, gic_v3_discovery_errors::unsupported_cells);
            return false;
        }
        if (name == "#address-cells") node.child_address_cells = static_cast<std::uint8_t>(raw);
        else node.child_size_cells = static_cast<std::uint8_t>(raw);
        return true;
    }

    if (name == "compatible") {
        node.gic_v3 = compatible_contains(value, "arm,gic-v3");
        return true;
    }
    if (name == "interrupt-controller") {
        node.interrupt_controller = true;
        return true;
    }
    if (name == "reg") {
        node.has_reg = true;
        node.reg = value;
        return true;
    }
    if (name == "redistributor-stride") {
        if (value.size() != 8U || !read_cells(value, 0U, 2U, node.redistributor_stride) ||
            node.redistributor_stride == 0ULL ||
            (node.redistributor_stride % 0x10000ULL) != 0ULL) {
            fail(context, gic_v3_discovery_errors::malformed);
            return false;
        }
        return true;
    }
    if (name == "#redistributor-regions") {
        if (value.size() != 4U ||
            !read_be32(value, 0U, node.declared_redistributor_regions) ||
            node.declared_redistributor_regions == 0U ||
            node.declared_redistributor_regions > GicV3Discovery::max_redistributor_regions) {
            fail(context, gic_v3_discovery_errors::too_many_redistributor_regions);
            return false;
        }
        return true;
    }
    return true;
}

bool end_node(void* opaque, std::size_t depth) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, gic_v3_discovery_errors::malformed);
        return false;
    }
    const auto& node = context.nodes[depth];
    if (!node.gic_v3) return true;
    if (!node.interrupt_controller || !node.has_reg ||
        node.parent_address_cells == 0U || node.parent_address_cells > 2U ||
        node.parent_size_cells == 0U || node.parent_size_cells > 2U) {
        fail(context, gic_v3_discovery_errors::malformed);
        return false;
    }
    if (++context.match_count != 1U) {
        fail(context, gic_v3_discovery_errors::ambiguous);
        return false;
    }

    const std::size_t tuple_bytes =
        static_cast<std::size_t>(node.parent_address_cells + node.parent_size_cells) * 4U;
    if (tuple_bytes == 0U || node.reg.size() < 2U * tuple_bytes ||
        node.reg.size() % tuple_bytes != 0U) {
        fail(context, gic_v3_discovery_errors::malformed);
        return false;
    }

    const std::size_t tuple_count = node.reg.size() / tuple_bytes;
    std::size_t redistributor_count = node.declared_redistributor_regions == 0U
        ? 1U
        : static_cast<std::size_t>(node.declared_redistributor_regions);
    if (redistributor_count > GicV3Discovery::max_redistributor_regions ||
        tuple_count < 1U + redistributor_count) {
        fail(context, gic_v3_discovery_errors::inconsistent_region_count);
        return false;
    }

    auto decode_range = [&](std::size_t tuple, HardwareRange& out) noexcept -> bool {
        const std::size_t base_offset = tuple * tuple_bytes;
        std::uint64_t base = 0ULL;
        std::uint64_t size = 0ULL;
        if (!read_cells(node.reg, base_offset, node.parent_address_cells, base) ||
            !read_cells(
                node.reg,
                base_offset + static_cast<std::size_t>(node.parent_address_cells) * 4U,
                node.parent_size_cells,
                size)) return false;
        out = HardwareRange{base, size};
        return out.valid();
    };

    if (!decode_range(0U, context.result.distributor)) {
        fail(context, gic_v3_discovery_errors::malformed);
        return false;
    }
    for (std::size_t i = 0U; i < redistributor_count; ++i) {
        if (!decode_range(1U + i, context.result.redistributors[i])) {
            fail(context, gic_v3_discovery_errors::malformed);
            return false;
        }
    }
    context.result.redistributor_count = redistributor_count;
    context.result.redistributor_stride = node.redistributor_stride;
    context.result.declared_redistributor_regions = node.declared_redistributor_regions;
    return true;
}

} // namespace

os::core::Result<GicV3Discovery>
discover_gic_v3(const FdtView& fdt) noexcept {
    Context context{};
    const FdtCallbacks callbacks{&context, begin_node, property, end_node};
    auto walked = fdt.walk(callbacks);
    if (context.failed) return context.error;
    if (!walked) return walked.error();
    if (context.match_count == 0U) return gic_error(gic_v3_discovery_errors::not_found);
    if (!context.result.valid()) return gic_error(gic_v3_discovery_errors::malformed);
    return context.result;
}

} // namespace os::kernel
