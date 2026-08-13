#include <os/kernel/fdt.hpp>
#include <os/kernel/gic_v3_discovery.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }

void put_be32(std::byte* p, std::uint32_t value) {
    p[0] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    p[1] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    p[2] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    p[3] = static_cast<std::byte>(value & 0xFFU);
}

struct Builder final {
    std::array<std::byte, 1024U> blob{};
    std::size_t cursor {56U}; // 40-byte header + zero reservation terminator.

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
    constexpr std::uint32_t off_compatible = 27U;
    constexpr std::uint32_t off_reg = 38U;
    constexpr std::uint32_t off_interrupt_controller = 42U;
    constexpr std::uint32_t off_regions = 63U;
    constexpr std::uint32_t off_stride = 86U;
    constexpr char strings[] =
        "#address-cells\0"
        "#size-cells\0"
        "compatible\0"
        "reg\0"
        "interrupt-controller\0"
        "#redistributor-regions\0"
        "redistributor-stride\0";

    Builder b{};
    constexpr std::uint32_t structure_offset = 56U;
    b.u32(FdtView::token_begin_node);
    b.padded_string("");

    std::array<std::byte, 4U> two{};
    put_be32(two.data(), 2U);
    b.property(off_address_cells, two.data(), two.size());
    b.property(off_size_cells, two.data(), two.size());

    // A sibling with cells this parser cannot represent as a decodable reg -
    // /cpus declares #size-cells = <0> in every real ARM device tree, because
    // a CPU's reg is an identifier rather than a range. QEMU virt always has
    // this node. It has nothing to do with the GIC and must not poison
    // discovery of the node that does - the exact regression that shipped
    // in M7.5g and produced a completely silent boot hang under real QEMU,
    // caught only because kernel-arm64-native's serial log came back empty.
    std::array<std::byte, 4U> one{};
    put_be32(one.data(), 1U);
    std::array<std::byte, 4U> zero{};
    put_be32(zero.data(), 0U);
    b.u32(FdtView::token_begin_node);
    b.padded_string("cpus");
    b.property(off_address_cells, one.data(), one.size());
    b.property(off_size_cells, zero.data(), zero.size());
    b.u32(FdtView::token_end_node);

    b.u32(FdtView::token_begin_node);
    b.padded_string("interrupt-controller@8000000");

    constexpr char compat[] = "vendor,gic-v3\0arm,gic-v3\0";
    std::array<std::byte, sizeof(compat) - 1U> compatible{};
    for (std::size_t i = 0U; i < compatible.size(); ++i) {
        compatible[i] = static_cast<std::byte>(compat[i]);
    }
    b.property(off_compatible, compatible.data(), compatible.size());
    b.property(off_interrupt_controller, nullptr, 0U);

    std::array<std::byte, 4U> regions{};
    put_be32(regions.data(), 2U);
    b.property(off_regions, regions.data(), regions.size());

    std::array<std::byte, 8U> stride{};
    put_be32(stride.data(), 0U);
    put_be32(stride.data() + 4U, 0x00020000U);
    b.property(off_stride, stride.data(), stride.size());

    // 2 address cells + 2 size cells per tuple. GICD followed by two GICR
    // regions. Any later optional GICC/GICH/GICV tuples are irrelevant here.
    std::array<std::byte, 48U> reg{};
    const std::array<std::uint64_t, 6U> values{
        0x08000000ULL, 0x00010000ULL,
        0x080A0000ULL, 0x00200000ULL,
        0x082A0000ULL, 0x00200000ULL,
    };
    for (std::size_t tuple = 0U; tuple < 3U; ++tuple) {
        const auto base = values[tuple * 2U];
        const auto size = values[tuple * 2U + 1U];
        const std::size_t o = tuple * 16U;
        put_be32(reg.data() + o, static_cast<std::uint32_t>(base >> 32U));
        put_be32(reg.data() + o + 4U, static_cast<std::uint32_t>(base));
        put_be32(reg.data() + o + 8U, static_cast<std::uint32_t>(size >> 32U));
        put_be32(reg.data() + o + 12U, static_cast<std::uint32_t>(size));
    }
    b.property(off_reg, reg.data(), reg.size());
    b.u32(FdtView::token_end_node);

    b.u32(FdtView::token_end_node);
    b.u32(FdtView::token_end);

    const std::size_t structure_size = b.cursor - structure_offset;
    const std::size_t strings_offset = b.cursor;
    for (std::size_t i = 0U; i < sizeof(strings); ++i) {
        b.blob[b.cursor++] = static_cast<std::byte>(strings[i]);
    }
    const std::size_t total_size = b.cursor;

    put_be32(b.blob.data() + 0U, FdtView::magic);
    put_be32(b.blob.data() + 4U, static_cast<std::uint32_t>(total_size));
    put_be32(b.blob.data() + 8U, structure_offset);
    put_be32(b.blob.data() + 12U, static_cast<std::uint32_t>(strings_offset));
    put_be32(b.blob.data() + 16U, 40U);
    put_be32(b.blob.data() + 20U, 17U);
    put_be32(b.blob.data() + 24U, 16U);
    put_be32(b.blob.data() + 28U, 0U);
    put_be32(b.blob.data() + 32U, static_cast<std::uint32_t>(sizeof(strings)));
    put_be32(b.blob.data() + 36U, static_cast<std::uint32_t>(structure_size));

    auto fdt = FdtView::parse(os::core::ByteSpan{b.blob.data(), total_size});
    require(static_cast<bool>(fdt));
    auto gic = discover_gic_v3(fdt.value());
    require(static_cast<bool>(gic));
    require(gic.value().valid());
    require(gic.value().distributor.base == 0x08000000ULL);
    require(gic.value().distributor.size == 0x00010000ULL);
    require(gic.value().redistributor_count == 2U);
    require(gic.value().redistributors[0].base == 0x080A0000ULL);
    require(gic.value().redistributors[1].base == 0x082A0000ULL);
    require(gic.value().declared_redistributor_regions == 2U);
    require(gic.value().redistributor_stride == 0x00020000ULL);

    return 0;
}
