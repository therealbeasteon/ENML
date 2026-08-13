#include <os/kernel/fdt.hpp>

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

void put_be64(std::byte* p, std::uint64_t value) {
    put_be32(p, static_cast<std::uint32_t>(value >> 32U));
    put_be32(p + 4U, static_cast<std::uint32_t>(value));
}

struct Seen final {
    std::size_t begin_nodes {0U};
    std::size_t properties {0U};
    std::size_t end_nodes {0U};
    std::size_t reservations {0U};
    bool saw_compatible {false};
};

bool on_begin(void* context, std::string_view name, std::size_t depth) noexcept {
    auto& seen = *static_cast<Seen*>(context);
    ++seen.begin_nodes;
    require(depth == 0U);
    require(name.empty());
    return true;
}

bool on_property(
    void* context,
    std::string_view name,
    os::core::ByteSpan value,
    std::size_t depth) noexcept {
    auto& seen = *static_cast<Seen*>(context);
    ++seen.properties;
    require(depth == 0U);
    if (name == "compatible") {
        seen.saw_compatible = true;
        require(value.size() == 12U);
    }
    return true;
}

bool on_end(void* context, std::size_t depth) noexcept {
    auto& seen = *static_cast<Seen*>(context);
    ++seen.end_nodes;
    require(depth == 0U);
    return true;
}

bool on_reservation(void* context, std::uint64_t address, std::uint64_t size) noexcept {
    auto& seen = *static_cast<Seen*>(context);
    ++seen.reservations;
    require(address == 0x0000'0000'4100'0000ULL);
    require(size == 0x0000'0000'0010'0000ULL);
    return true;
}
}

int main() {
    using namespace os::kernel;

    // header + one reservation + terminator + structure + strings.
    std::array<std::byte, 123U> blob{};
    constexpr std::uint32_t reservation_offset = 40U;
    constexpr std::uint32_t structure_offset = 72U;
    constexpr std::uint32_t structure_size = 40U;
    constexpr std::uint32_t strings_offset = 112U;
    constexpr std::uint32_t strings_size = 11U;

    put_be32(blob.data() + 0U, FdtView::magic);
    put_be32(blob.data() + 4U, static_cast<std::uint32_t>(blob.size()));
    put_be32(blob.data() + 8U, structure_offset);
    put_be32(blob.data() + 12U, strings_offset);
    put_be32(blob.data() + 16U, reservation_offset);
    put_be32(blob.data() + 20U, 17U);
    put_be32(blob.data() + 24U, 16U);
    put_be32(blob.data() + 28U, 0U);
    put_be32(blob.data() + 32U, strings_size);
    put_be32(blob.data() + 36U, structure_size);

    put_be64(blob.data() + reservation_offset, 0x4100'0000ULL);
    put_be64(blob.data() + reservation_offset + 8U, 0x0010'0000ULL);
    // Second reservation entry is already the all-zero terminator.

    std::size_t cursor = structure_offset;
    put_be32(blob.data() + cursor, FdtView::token_begin_node); cursor += 4U;
    cursor += 4U;
    put_be32(blob.data() + cursor, FdtView::token_property); cursor += 4U;
    put_be32(blob.data() + cursor, 12U); cursor += 4U;
    put_be32(blob.data() + cursor, 0U); cursor += 4U;
    constexpr char compatible_value[] = "cookie,test";
    for (std::size_t i = 0U; i < sizeof(compatible_value); ++i) {
        blob[cursor + i] = static_cast<std::byte>(compatible_value[i]);
    }
    cursor += 12U;
    put_be32(blob.data() + cursor, FdtView::token_end_node); cursor += 4U;
    put_be32(blob.data() + cursor, FdtView::token_end); cursor += 4U;
    require(cursor == strings_offset);

    constexpr char compatible_name[] = "compatible";
    for (std::size_t i = 0U; i < sizeof(compatible_name); ++i) {
        blob[strings_offset + i] = static_cast<std::byte>(compatible_name[i]);
    }

    auto parsed = FdtView::parse(blob);
    require(static_cast<bool>(parsed));
    require(parsed.value().version() == 17U);
    require(parsed.value().total_size() == blob.size());

    Seen seen{};
    auto reservations = parsed.value().walk_reservations({&seen, on_reservation});
    require(static_cast<bool>(reservations));
    require(seen.reservations == 1U);

    const FdtCallbacks callbacks{&seen, on_begin, on_property, on_end};
    auto walked = parsed.value().walk(callbacks);
    require(static_cast<bool>(walked));
    require(seen.begin_nodes == 1U);
    require(seen.properties == 1U);
    require(seen.end_nodes == 1U);
    require(seen.saw_compatible);

    auto bad_magic = blob;
    bad_magic[0] = std::byte{0};
    require(!FdtView::parse(bad_magic));

    auto bad_bounds = blob;
    put_be32(bad_bounds.data() + 4U, 0xFFFFU);
    require(!FdtView::parse(bad_bounds));

    auto bad_reservation = blob;
    put_be64(bad_reservation.data() + reservation_offset + 16U, 1ULL);
    put_be64(bad_reservation.data() + reservation_offset + 24U, 0ULL);
    require(!FdtView::parse(bad_reservation));

    auto bad_token = blob;
    put_be32(bad_token.data() + structure_offset, 0x77U);
    auto malformed = FdtView::parse(bad_token);
    require(static_cast<bool>(malformed));
    require(!malformed.value().walk({}));

    return 0;
}
