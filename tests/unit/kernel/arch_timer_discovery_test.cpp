#include <os/kernel/arch_timer_discovery.hpp>
#include <os/kernel/fdt.hpp>

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
    std::array<std::byte, 768U> blob{};
    std::size_t cursor {56U};
    void u32(std::uint32_t value) { put_be32(blob.data() + cursor, value); cursor += 4U; }
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

    constexpr std::uint32_t off_compatible = 0U;
    constexpr std::uint32_t off_interrupts = 11U;
    constexpr std::uint32_t off_interrupt_names = 22U;
    constexpr char strings[] =
        "compatible\0"
        "interrupts\0"
        "interrupt-names\0";

    Builder b{};
    constexpr std::uint32_t structure_offset = 56U;
    b.u32(FdtView::token_begin_node);
    b.padded_string("");
    b.u32(FdtView::token_begin_node);
    b.padded_string("timer");

    constexpr char compatible_text[] = "arm,armv8-timer\0arm,armv7-timer\0";
    std::array<std::byte, sizeof(compatible_text) - 1U> compatible{};
    for (std::size_t i = 0U; i < compatible.size(); ++i) {
        compatible[i] = static_cast<std::byte>(compatible_text[i]);
    }
    b.property(off_compatible, compatible.data(), compatible.size());

    constexpr char names_text[] = "sec-phys\0phys\0virt\0hyp-phys\0";
    std::array<std::byte, sizeof(names_text) - 1U> names{};
    for (std::size_t i = 0U; i < names.size(); ++i) names[i] = static_cast<std::byte>(names_text[i]);
    b.property(off_interrupt_names, names.data(), names.size());

    // Four standard GIC 3-cell specifiers. The named non-secure physical timer
    // is PPI 14, which becomes architectural INTID 30.
    std::array<std::byte, 48U> interrupts{};
    constexpr std::array<std::uint32_t, 4U> ppis{13U, 14U, 11U, 10U};
    for (std::size_t i = 0U; i < ppis.size(); ++i) {
        const auto o = i * 12U;
        put_be32(interrupts.data() + o, 1U);
        put_be32(interrupts.data() + o + 4U, ppis[i]);
        put_be32(interrupts.data() + o + 8U, 4U);
    }
    b.property(off_interrupts, interrupts.data(), interrupts.size());

    b.u32(FdtView::token_end_node);
    b.u32(FdtView::token_end_node);
    b.u32(FdtView::token_end);

    const std::size_t structure_size = b.cursor - structure_offset;
    const std::size_t strings_offset = b.cursor;
    for (std::size_t i = 0U; i < sizeof(strings); ++i) b.blob[b.cursor++] = static_cast<std::byte>(strings[i]);
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
    auto timer = discover_architected_timer(fdt.value());
    require(static_cast<bool>(timer));
    require(timer.value().valid());
    require(timer.value().nonsecure_physical_intid == 30U);
    require((timer.value().trigger_flags & 0xFU) == 4U);
    return 0;
}
