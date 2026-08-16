#include <os/image/ckx.hpp>

#include <os/core/error.hpp>

namespace os::image {
namespace {

[[nodiscard]] constexpr os::core::Error ckx_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::image, code);
}

// Little-endian, byte at a time. Deliberately not a packed-struct overlay: that
// would make the on-disk layout depend on the compiler agreeing with the
// format, and agreement is the one thing a parser over untrusted bytes must not
// depend on.
[[nodiscard]] std::uint16_t read_u16(
    std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U));
}

[[nodiscard]] std::uint64_t read_u64(
    std::span<const std::byte> bytes, std::size_t offset) noexcept {
    std::uint64_t value = 0ULL;
    for (std::size_t i = 0U; i < 8U; ++i) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + i]))
                 << (8U * i);
    }
    return value;
}

[[nodiscard]] bool zero_range(
    std::span<const std::byte> bytes, std::size_t offset, std::size_t length) noexcept {
    for (std::size_t i = 0U; i < length; ++i) {
        if (bytes[offset + i] != std::byte{0}) return false;
    }
    return true;
}

// base + length - 1, without ever computing base + length. The natural form
// wraps, and a wrapped comparison says yes.
[[nodiscard]] bool range_within(
    std::uint64_t base, std::uint64_t length, std::uint64_t limit) noexcept {
    if (length == 0ULL) return false;
    if (base > limit) return false;
    return length <= (limit - base);
}

[[nodiscard]] bool ranges_overlap(
    std::uint64_t a_base, std::uint64_t a_length,
    std::uint64_t b_base, std::uint64_t b_length) noexcept {
    if (a_length == 0ULL || b_length == 0ULL) return false;
    const std::uint64_t a_last = a_base + (a_length - 1ULL);
    const std::uint64_t b_last = b_base + (b_length - 1ULL);
    return a_base <= b_last && b_base <= a_last;
}

[[nodiscard]] bool valid_permissions(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(CkxPermissions::read) ||
           raw == static_cast<std::uint8_t>(CkxPermissions::read_write) ||
           raw == static_cast<std::uint8_t>(CkxPermissions::read_execute);
}

[[nodiscard]] bool valid_disclosure(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(CkxDisclosure::paged) ||
           raw == static_cast<std::uint8_t>(CkxDisclosure::sealed);
}

[[nodiscard]] bool valid_content(std::uint8_t raw) noexcept {
    return raw == static_cast<std::uint8_t>(CkxContent::anonymous) ||
           raw == static_cast<std::uint8_t>(CkxContent::named);
}

// AArch64 stage-1, 4 KiB granule, 39-bit virtual addresses: level 1 covers
// 1 GiB per entry, level 2 covers 2 MiB, level 3 covers a page.
inline constexpr std::uint64_t level1_span = 1ULL << 30U;
inline constexpr std::uint64_t level2_span = 1ULL << 21U;

// How many distinct spans of `span` bytes the half-open range touches.
[[nodiscard]] std::uint64_t spans_touched(
    std::uint64_t base, std::uint64_t length, std::uint64_t span) noexcept {
    if (length == 0ULL) return 0ULL;
    const std::uint64_t first = base / span;
    const std::uint64_t last = (base + (length - 1ULL)) / span;
    return (last - first) + 1ULL;
}

} // namespace

CkxConstructionCost aarch64_construction_cost(const CkxImage& image) noexcept {
    CkxConstructionCost cost{};
    // The root, always. A space exists before anything is mapped into it.
    std::uint64_t level2_tables = 0ULL;
    std::uint64_t level3_tables = 0ULL;

    for (const auto& region : image.used()) {
        cost.backing_pages += region.pages();
        // Deliberately an over-count when two regions share a table: this is a
        // budget a caller must be able to meet, and a budget that is sometimes
        // too small is worse than one that is sometimes too large. Two regions
        // in the same 2 MiB span are charged two level-3 tables and will use
        // one; the caller donates a page it did not need, which is recovered by
        // reclamation when the space is destroyed. The reverse error strands a
        // half-built space, which is the failure this function exists to
        // prevent.
        level2_tables += spans_touched(region.virtual_address, region.length, level1_span);
        level3_tables += spans_touched(region.virtual_address, region.length, level2_span);
    }
    cost.table_pages = 1ULL + level2_tables + level3_tables;
    return cost;
}

os::core::Result<CkxImage> parse_ckx(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < ckx_header_bytes) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::truncated)};
    }
    for (std::size_t i = 0U; i < ckx_magic_bytes; ++i) {
        if (bytes[i] != ckx_magic[i]) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::bad_magic)};
        }
    }

    CkxImage image{};
    image.format_version = read_u16(bytes, 4U);
    // Refused, not tolerated. An image naming a version this build does not
    // implement is one whose fields mean something else.
    if (image.format_version != ckx_format_version_1) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::unsupported_version)};
    }

    const std::uint16_t declared_regions = read_u16(bytes, 6U);
    if (declared_regions == 0U || declared_regions > max_ckx_regions) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::region_count)};
    }
    image.region_count = static_cast<std::size_t>(declared_regions);

    image.entry = read_u64(bytes, 8U);
    image.authority_ceiling = read_u64(bytes, 16U);
    if (!zero_range(bytes, 24U, 8U)) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::reserved_not_zero)};
    }
    // A ceiling naming a class that does not exist is refused rather than
    // masked down to the classes that do. The ceiling is signed content, and
    // the right answer to "I do not understand what you asked for" is to refuse
    // rather than to guess a smaller version of it.
    if (image.authority_ceiling == 0ULL ||
        (image.authority_ceiling & ~ckx_authority::all) != 0ULL) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::invalid_authority)};
    }
    // A64 instructions are four bytes and four-byte aligned - the same check
    // the translation-root sealer makes, for the same reason: an unaligned
    // entry names a point inside an instruction. docs/M7_12_ENTRY_BINDING.md.
    if (image.entry == 0ULL || (image.entry & 0x3ULL) != 0ULL) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::invalid_entry)};
    }

    const std::uint64_t table_bytes =
        static_cast<std::uint64_t>(image.region_count) * ckx_region_bytes;
    if (!range_within(ckx_header_bytes, table_bytes,
                      static_cast<std::uint64_t>(bytes.size()))) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::truncated)};
    }

    for (std::size_t index = 0U; index < image.region_count; ++index) {
        const std::size_t base = ckx_header_bytes + index * ckx_region_bytes;
        CkxRegion region{};
        region.virtual_address = read_u64(bytes, base + 0U);
        region.length = read_u64(bytes, base + 8U);

        const auto permissions = std::to_integer<std::uint8_t>(bytes[base + 16U]);
        const auto disclosure = std::to_integer<std::uint8_t>(bytes[base + 17U]);
        const auto content = std::to_integer<std::uint8_t>(bytes[base + 18U]);
        if (!valid_permissions(permissions)) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::invalid_permissions)};
        }
        if (!valid_disclosure(disclosure)) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::invalid_disclosure)};
        }
        if (!valid_content(content)) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::invalid_content)};
        }
        if (!zero_range(bytes, base + 19U, 5U)) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::reserved_not_zero)};
        }
        region.permissions = static_cast<CkxPermissions>(permissions);
        region.disclosure = static_cast<CkxDisclosure>(disclosure);
        region.content = static_cast<CkxContent>(content);
        for (std::size_t i = 0U; i < ckx_digest_bytes; ++i) {
            region.digest[i] = bytes[base + 24U + i];
        }

        // A region becomes mappings, and Cookie maps pages. Non-empty,
        // page-aligned in both address and length, and not wrapping.
        if (region.length == 0ULL ||
            (region.length % ckx_page_bytes) != 0ULL ||
            (region.virtual_address % ckx_page_bytes) != 0ULL ||
            region.virtual_address > UINT64_MAX - (region.length - 1ULL)) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::region_range)};
        }

        // The digest and the content kind are two statements about the same
        // thing, and each value contradicts the other's opposite. An anonymous
        // region carrying a digest claims content it is defined not to have; a
        // named region with a zero digest names nothing while claiming to name
        // something.
        const bool digest_is_zero = [&region]() noexcept {
            for (const auto value : region.digest) {
                if (value != std::byte{0}) return false;
            }
            return true;
        }();
        if ((region.content == CkxContent::anonymous) != digest_is_zero) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::invalid_content)};
        }

        // The rule this format exists to make unstateable: no executable region
        // may be anonymous. "Execute whatever happens to be there" is not
        // something a Cookie program can say. Every byte that will ever be
        // fetched as an instruction is named by a digest somebody signed.
        if (region.permissions == CkxPermissions::read_execute &&
            region.content != CkxContent::named) {
            return os::core::Result<CkxImage>{ckx_error(ckx_errors::anonymous_executable)};
        }

        for (std::size_t other = 0U; other < index; ++other) {
            const auto& previous = image.regions[other];
            if (ranges_overlap(region.virtual_address, region.length,
                               previous.virtual_address, previous.length)) {
                return os::core::Result<CkxImage>{ckx_error(ckx_errors::region_overlap)};
            }
        }
        image.regions[index] = region;
    }

    // The entry must land in a region this image itself makes executable.
    // Without this an image could name an entry its own plan does not make
    // runnable, and the failure would surface as an instruction abort inside a
    // process rather than as a refusal to accept a malformed image.
    bool entry_is_executable = false;
    for (std::size_t index = 0U; index < image.region_count; ++index) {
        const auto& region = image.regions[index];
        if (region.permissions != CkxPermissions::read_execute) continue;
        if (region.virtual_address <= image.entry &&
            image.entry - region.virtual_address < region.length) {
            entry_is_executable = true;
            break;
        }
    }
    if (!entry_is_executable) {
        return os::core::Result<CkxImage>{ckx_error(ckx_errors::entry_not_executable)};
    }

    return image;
}

} // namespace os::image
