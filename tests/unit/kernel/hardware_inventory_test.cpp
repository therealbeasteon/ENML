#include <os/kernel/fdt.hpp>
#include <os/kernel/hardware_inventory.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace {
void require(bool value) { if (!value) std::abort(); }

void put_be32(std::byte* p, std::uint32_t value) {
    p[0] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    p[1] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    p[2] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    p[3] = static_cast<std::byte>(value & 0xFFU);
}

struct Builder final {
    std::array<std::byte, 512U> blob{};
    std::size_t cursor {40U};

    void u32(std::uint32_t value) {
        put_be32(blob.data() + cursor, value);
        cursor += 4U;
    }
    void padded_string(std::string_view text) {
        for (char c : text) blob[cursor++] = static_cast<std::byte>(c);
        blob[cursor++] = std::byte{0};
        while ((cursor & 3U) != 0U) blob[cursor++] = std::byte{0};
    }
    void property(std::uint32_t name_offset, const std::byte* value, std::size_t length) {
        u32(os::kernel::FdtView::token_property);
        u32(static_cast<std::uint32_t>(length));
        u32(name_offset);
        for (std::size_t i = 0U; i < length; ++i) blob[cursor++] = value[i];
        while ((cursor & 3U) != 0U) blob[cursor++] = std::byte{0};
    }
};
}

int main() {
    using namespace os::kernel;

    constexpr std::uint32_t off_address_cells = 0U;
    constexpr std::uint32_t off_size_cells = 15U;
    constexpr std::uint32_t off_device_type = 27U;
    constexpr std::uint32_t off_reg = 39U;
    constexpr std::uint32_t off_compatible = 43U;
    constexpr char strings[] =
        "#address-cells\0"
        "#size-cells\0"
        "device_type\0"
        "reg\0"
        "compatible\0";

    Builder b{};
    b.u32(FdtView::token_begin_node);
    b.padded_string("");

    std::array<std::byte, 4U> two{};
    put_be32(two.data(), 2U);
    b.property(off_address_cells, two.data(), two.size());
    b.property(off_size_cells, two.data(), two.size());

    b.u32(FdtView::token_begin_node);
    b.padded_string("memory@40000000");
    constexpr char memory_type[] = "memory";
    std::array<std::byte, sizeof(memory_type)> memory_value{};
    for (std::size_t i = 0U; i < sizeof(memory_type); ++i) {
        memory_value[i] = static_cast<std::byte>(memory_type[i]);
    }
    b.property(off_device_type, memory_value.data(), memory_value.size());
    std::array<std::byte, 16U> memory_reg{};
    put_be32(memory_reg.data() + 0U, 0U);
    put_be32(memory_reg.data() + 4U, 0x40000000U);
    put_be32(memory_reg.data() + 8U, 0U);
    put_be32(memory_reg.data() + 12U, 0x08000000U);
    b.property(off_reg, memory_reg.data(), memory_reg.size());
    b.u32(FdtView::token_end_node);

    b.u32(FdtView::token_begin_node);
    b.padded_string("uart@9000000");
    constexpr char compatible[] = "arm,pl011";
    std::array<std::byte, sizeof(compatible)> compatible_value{};
    for (std::size_t i = 0U; i < sizeof(compatible); ++i) {
        compatible_value[i] = static_cast<std::byte>(compatible[i]);
    }
    b.property(off_compatible, compatible_value.data(), compatible_value.size());
    std::array<std::byte, 16U> uart_reg{};
    put_be32(uart_reg.data() + 0U, 0U);
    put_be32(uart_reg.data() + 4U, 0x09000000U);
    put_be32(uart_reg.data() + 8U, 0U);
    put_be32(uart_reg.data() + 12U, 0x00001000U);
    b.property(off_reg, uart_reg.data(), uart_reg.size());
    b.u32(FdtView::token_end_node);

    b.u32(FdtView::token_end_node);
    b.u32(FdtView::token_end);

    const std::size_t structure_size = b.cursor - 40U;
    const std::size_t strings_offset = b.cursor;
    for (std::size_t i = 0U; i < sizeof(strings); ++i) {
        b.blob[b.cursor++] = static_cast<std::byte>(strings[i]);
    }
    const std::size_t total_size = b.cursor;

    put_be32(b.blob.data() + 0U, FdtView::magic);
    put_be32(b.blob.data() + 4U, static_cast<std::uint32_t>(total_size));
    put_be32(b.blob.data() + 8U, 40U);
    put_be32(b.blob.data() + 12U, static_cast<std::uint32_t>(strings_offset));
    put_be32(b.blob.data() + 16U, 40U);
    put_be32(b.blob.data() + 20U, 17U);
    put_be32(b.blob.data() + 24U, 16U);
    put_be32(b.blob.data() + 28U, 0U);
    put_be32(b.blob.data() + 32U, static_cast<std::uint32_t>(sizeof(strings)));
    put_be32(b.blob.data() + 36U, static_cast<std::uint32_t>(structure_size));

    auto fdt = FdtView::parse(os::core::ByteSpan{b.blob.data(), total_size});
    require(static_cast<bool>(fdt));
    auto inventory = discover_hardware(fdt.value());
    require(static_cast<bool>(inventory));
    require(inventory.value().memory_count == 1U);
    require(inventory.value().memory[0].base == 0x40000000ULL);
    require(inventory.value().memory[0].size == 0x08000000ULL);
    require(inventory.value().device_count == 1U);
    require(inventory.value().devices[0].compatible_view() == "arm,pl011");
    require(inventory.value().devices[0].registers.base == 0x09000000ULL);
    require(inventory.value().devices[0].registers.size == 0x1000ULL);

    return 0;
}
