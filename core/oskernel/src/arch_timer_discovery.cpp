#include <os/kernel/arch_timer_discovery.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error timer_error(std::uint32_t code) noexcept {
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

[[nodiscard]] bool compatible_contains(
    os::core::ByteSpan value,
    std::string_view wanted) noexcept {
    std::size_t offset = 0U;
    while (offset < value.size()) {
        std::size_t end = offset;
        while (end < value.size() && value[end] != std::byte{0}) ++end;
        if (end == value.size()) return false;
        if (end - offset == wanted.size()) {
            bool equal = true;
            for (std::size_t i = 0U; i < wanted.size(); ++i) {
                if (std::to_integer<char>(value[offset + i]) != wanted[i]) {
                    equal = false;
                    break;
                }
            }
            if (equal) return true;
        }
        offset = end + 1U;
    }
    return false;
}

struct NodeState final {
    bool timer {false};
    os::core::ByteSpan interrupts {};
    os::core::ByteSpan interrupt_names {};
};

struct Context final {
    static constexpr std::size_t max_depth = 16U;
    std::array<NodeState, max_depth> nodes {};
    ArchitectedTimerDiscovery result {};
    std::size_t match_count {0U};
    os::core::Error error {};
    bool failed {false};
};

void fail(Context& context, std::uint32_t code) noexcept {
    if (!context.failed) {
        context.error = timer_error(code);
        context.failed = true;
    }
}

bool begin_node(void* opaque, std::string_view, std::size_t depth) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, arch_timer_discovery_errors::malformed);
        return false;
    }
    context.nodes[depth] = NodeState{};
    return true;
}

bool property(
    void* opaque,
    std::string_view name,
    os::core::ByteSpan value,
    std::size_t depth) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, arch_timer_discovery_errors::malformed);
        return false;
    }
    auto& node = context.nodes[depth];
    if (name == "compatible") {
        node.timer = compatible_contains(value, "arm,armv8-timer") ||
            compatible_contains(value, "arm,armv7-timer") ||
            compatible_contains(value, "arm,cortex-a15-timer");
    } else if (name == "interrupts") {
        node.interrupts = value;
    } else if (name == "interrupt-names") {
        node.interrupt_names = value;
    }
    return true;
}

[[nodiscard]] bool name_index(
    os::core::ByteSpan names,
    std::string_view wanted,
    std::size_t& index) noexcept {
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < names.size()) {
        std::size_t end = offset;
        while (end < names.size() && names[end] != std::byte{0}) ++end;
        if (end == names.size()) return false;
        if (end - offset == wanted.size()) {
            bool equal = true;
            for (std::size_t i = 0U; i < wanted.size(); ++i) {
                if (std::to_integer<char>(names[offset + i]) != wanted[i]) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                index = current;
                return true;
            }
        }
        offset = end + 1U;
        ++current;
    }
    return false;
}

[[nodiscard]] bool decode_timer_entry(
    os::core::ByteSpan interrupts,
    std::size_t index,
    std::uint32_t& intid,
    std::uint32_t& raw_trigger_flags,
    std::uint32_t& error_code) noexcept {
    const std::size_t offset = index * 12U;
    std::uint32_t type = 0U;
    std::uint32_t number = 0U;
    std::uint32_t flags = 0U;
    if (!read_be32(interrupts, offset, type) ||
        !read_be32(interrupts, offset + 4U, number) ||
        !read_be32(interrupts, offset + 8U, flags)) {
        error_code = arch_timer_discovery_errors::malformed;
        return false;
    }

    if (type == 1U) {
        if (number > 15U) {
            error_code = arch_timer_discovery_errors::malformed;
            return false;
        }
        intid = 16U + number;
    } else if (type == 3U) {
        if (number > 63U) {
            error_code = arch_timer_discovery_errors::malformed;
            return false;
        }
        intid = 1056U + number;
    } else {
        error_code = arch_timer_discovery_errors::unsupported_interrupt_type;
        return false;
    }

    if (!architected_timer_trigger_supported(flags)) {
        error_code = arch_timer_discovery_errors::unsupported_trigger;
        return false;
    }
    raw_trigger_flags = flags;
    return true;
}

bool end_node(void* opaque, std::size_t depth) noexcept {
    auto& context = *static_cast<Context*>(opaque);
    if (depth >= context.nodes.size()) {
        fail(context, arch_timer_discovery_errors::malformed);
        return false;
    }
    const auto& node = context.nodes[depth];
    if (!node.timer) return true;
    if (++context.match_count != 1U) {
        fail(context, arch_timer_discovery_errors::ambiguous);
        return false;
    }
    if (node.interrupts.empty() || (node.interrupts.size() % 12U) != 0U) {
        fail(context, arch_timer_discovery_errors::unsupported_interrupt_cells);
        return false;
    }

    const std::size_t count = node.interrupts.size() / 12U;
    if (count < 2U) {
        fail(context, arch_timer_discovery_errors::malformed);
        return false;
    }

    std::size_t physical_index = 0U;
    std::size_t virtual_index = 0U;
    if (!node.interrupt_names.empty()) {
        if (!name_index(node.interrupt_names, "phys", physical_index) || physical_index >= count ||
            !name_index(node.interrupt_names, "virt", virtual_index) || virtual_index >= count) {
            fail(context, arch_timer_discovery_errors::malformed);
            return false;
        }
    } else {
        // Standard ARM ordering is sec-phys, phys, virt[, hyp-phys]. A node
        // naming its interrupts always takes the branch above on real
        // hardware and under QEMU virt; this is the defensive fallback for a
        // DTB that omits interrupt-names.
        physical_index = count >= 3U ? 1U : 0U;
        virtual_index = count >= 3U ? 2U : 1U;
        if (virtual_index >= count) {
            fail(context, arch_timer_discovery_errors::malformed);
            return false;
        }
    }

    std::uint32_t error_code = 0U;
    if (!decode_timer_entry(
            node.interrupts, physical_index,
            context.result.nonsecure_physical_intid,
            context.result.raw_trigger_flags, error_code)) {
        fail(context, error_code);
        return false;
    }
    context.result.trigger_flags = dt_irq_level_high;

    if (!decode_timer_entry(
            node.interrupts, virtual_index,
            context.result.virtual_intid,
            context.result.virtual_raw_trigger_flags, error_code)) {
        fail(context, error_code);
        return false;
    }
    context.result.virtual_trigger_flags = dt_irq_level_high;
    return true;
}

} // namespace

os::core::Result<ArchitectedTimerDiscovery>
discover_architected_timer(const FdtView& fdt) noexcept {
    Context context{};
    const FdtCallbacks callbacks{&context, begin_node, property, end_node};
    auto walked = fdt.walk(callbacks);
    if (context.failed) return context.error;
    if (!walked) return walked.error();
    if (context.match_count == 0U) return timer_error(arch_timer_discovery_errors::not_found);
    if (!context.result.valid()) return timer_error(arch_timer_discovery_errors::malformed);
    return context.result;
}

} // namespace os::kernel
