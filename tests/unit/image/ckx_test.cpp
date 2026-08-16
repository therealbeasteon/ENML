#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <os/core/error.hpp>
#include <os/image/ckx.hpp>

namespace {

using namespace os::image;

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "ckx: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::image &&
           result.error().code == code;
}

void put16(std::vector<std::byte>& out, std::size_t at, std::uint16_t value) {
    out[at] = static_cast<std::byte>(value & 0xFFU);
    out[at + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void put64(std::vector<std::byte>& out, std::size_t at, std::uint64_t value) {
    for (std::size_t i = 0U; i < 8U; ++i) {
        out[at + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFULL);
    }
}

constexpr std::uint64_t code_virtual = 0x0000'0000'1000'0000ULL;
constexpr std::uint64_t data_virtual = 0x0000'0000'1010'0000ULL;
constexpr std::size_t region0 = ckx_header_bytes;
constexpr std::size_t region1 = ckx_header_bytes + ckx_region_bytes;

// Two regions: named read-execute code holding the entry, and anonymous
// read-write data. The shape of an actual program.
std::vector<std::byte> well_formed() {
    std::vector<std::byte> out(ckx_header_bytes + 2U * ckx_region_bytes, std::byte{0});
    for (std::size_t i = 0U; i < ckx_magic_bytes; ++i) out[i] = ckx_magic[i];
    put16(out, 4U, ckx_format_version_1);
    put16(out, 6U, 2U);
    put64(out, 8U, code_virtual + 0x40U);
    put64(out, 16U, ckx_authority::unprivileged | ckx_authority::memory_control);

    put64(out, region0 + 0U, code_virtual);
    put64(out, region0 + 8U, 0x2000U);
    out[region0 + 16U] = static_cast<std::byte>(CkxPermissions::read_execute);
    out[region0 + 17U] = static_cast<std::byte>(CkxDisclosure::paged);
    out[region0 + 18U] = static_cast<std::byte>(CkxContent::named);
    out[region0 + 24U] = std::byte{0xA1};  // a non-zero digest

    put64(out, region1 + 0U, data_virtual);
    put64(out, region1 + 8U, 0x4000U);
    out[region1 + 16U] = static_cast<std::byte>(CkxPermissions::read_write);
    out[region1 + 17U] = static_cast<std::byte>(CkxDisclosure::sealed);
    out[region1 + 18U] = static_cast<std::byte>(CkxContent::anonymous);
    return out;
}

} // namespace

// The .ckx parser. Every assertion is one of the rules
// docs/M7_12_CKX_FORMAT.md states, because those rules are the format's whole
// security content and a parser that quietly stopped enforcing one would still
// accept every image anyone had ever built.
int main() {
    {
        auto image = parse_ckx(well_formed());
        if (!check(static_cast<bool>(image), "a well-formed image was refused")) return 1;
        if (!check(image.value().region_count == 2U, "wrong region count")) return 1;
        if (!check(image.value().entry == code_virtual + 0x40U, "entry lost")) return 1;
        if (!check(image.value().regions[0].pages() == 2U, "wrong page count")) return 1;
        if (!check(image.value().regions[1].disclosure == CkxDisclosure::sealed,
                   "a sealed region did not survive the parse")) return 1;
        if (!check(image.value().regions[1].content == CkxContent::anonymous,
                   "an anonymous region did not survive the parse")) return 1;

        // The cost is computed from the plan, not read out of it. That is the
        // whole reason it is a function: a declared cost is a number an image
        // can lie about, and the lie surfaces as an exhaustion partway through
        // building a space - the exact failure knowing the cost prevents.
        const auto cost = aarch64_construction_cost(image.value());
        if (!check(cost.backing_pages == 2U + 4U, "backing pages wrong")) return 1;
        // Root, plus one level-2 per gigabyte touched, plus one level-3 per
        // 2 MiB span. Both regions sit inside one gigabyte and in different
        // 2 MiB spans.
        if (!check(cost.table_pages == 1U + 2U + 2U, "table pages wrong")) return 1;
        if (!check(cost.total() == cost.backing_pages + cost.table_pages,
                   "total disagreed with its parts")) return 1;
    }

    {
        auto bytes = well_formed();
        bytes.resize(ckx_header_bytes - 1U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::truncated),
                   "a header shorter than the header was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        put16(bytes, 6U, static_cast<std::uint16_t>(max_ckx_regions));
        if (!check(refused(parse_ckx(bytes), ckx_errors::truncated),
                   "a region count larger than the file was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        bytes[2] = std::byte{'Z'};
        if (!check(refused(parse_ckx(bytes), ckx_errors::bad_magic),
                   "wrong magic was accepted")) return 1;
    }
    // An unknown version is refused rather than treated as the newest
    // understood one: a format that degrades gracefully is one an attacker
    // picks the version of.
    {
        auto bytes = well_formed();
        put16(bytes, 4U, ckx_format_version_1 + 1U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::unsupported_version),
                   "a future version was accepted")) return 1;
        put16(bytes, 4U, 0U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::unsupported_version),
                   "version zero was accepted")) return 1;
    }
    // Reserved fields must be zero in header and region alike. A reserved field
    // that is ignored is a covert channel through signed content.
    {
        auto bytes = well_formed();
        bytes[24U] = std::byte{1};
        if (!check(refused(parse_ckx(bytes), ckx_errors::reserved_not_zero),
                   "a non-zero header reserved field was ignored")) return 1;
    }
    {
        auto bytes = well_formed();
        bytes[region0 + 19U] = std::byte{1};
        if (!check(refused(parse_ckx(bytes), ckx_errors::reserved_not_zero),
                   "a non-zero region reserved field was ignored")) return 1;
    }
    {
        auto bytes = well_formed();
        put16(bytes, 6U, 0U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::region_count),
                   "an image with no regions was accepted")) return 1;
        put16(bytes, 6U, static_cast<std::uint16_t>(max_ckx_regions + 1U));
        if (!check(refused(parse_ckx(bytes), ckx_errors::region_count),
                   "more regions than the fixed table holds was accepted")) return 1;
    }

    // The authority ceiling. An unknown class is refused rather than masked
    // away: the ceiling is signed content, so the right answer to "I do not
    // understand what you asked for" is to refuse, not to guess something
    // smaller.
    {
        auto bytes = well_formed();
        put64(bytes, 16U, ckx_authority::all | (1ULL << 40U));
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_authority),
                   "an unknown authority class was masked away instead of refused")) return 1;
        put64(bytes, 16U, 0ULL);
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_authority),
                   "an empty ceiling was accepted")) return 1;
    }

    {
        auto bytes = well_formed();
        put64(bytes, 8U, code_virtual + 0x41U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_entry),
                   "an entry inside an instruction was accepted")) return 1;
        put64(bytes, 8U, 0ULL);
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_entry),
                   "a zero entry was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        put64(bytes, 8U, data_virtual);
        if (!check(refused(parse_ckx(bytes), ckx_errors::entry_not_executable),
                   "an entry in a non-executable region was accepted")) return 1;
        put64(bytes, 8U, code_virtual + 0x9000U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::entry_not_executable),
                   "an entry outside every region was accepted")) return 1;
    }

    {
        auto bytes = well_formed();
        bytes[region0 + 16U] = std::byte{9};
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_permissions),
                   "an unknown permission was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        bytes[region0 + 17U] = std::byte{9};
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_disclosure),
                   "an unknown disclosure class was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        bytes[region0 + 18U] = std::byte{9};
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_content),
                   "an unknown content kind was accepted")) return 1;
    }

    // Content kind and digest are two statements about the same thing, and each
    // value contradicts the other's opposite.
    {
        auto bytes = well_formed();
        bytes[region1 + 24U] = std::byte{0xFF};
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_content),
                   "an anonymous region carrying a digest was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        bytes[region0 + 24U] = std::byte{0};
        if (!check(refused(parse_ckx(bytes), ckx_errors::invalid_content),
                   "a named region with a zero digest was accepted")) return 1;
    }

    // The rule the format exists to make unstateable: no executable region may
    // be anonymous. Every byte that will ever be fetched as an instruction is
    // named by a digest somebody signed.
    {
        auto bytes = well_formed();
        bytes[region0 + 18U] = static_cast<std::byte>(CkxContent::anonymous);
        bytes[region0 + 24U] = std::byte{0};
        if (!check(refused(parse_ckx(bytes), ckx_errors::anonymous_executable),
                   "an anonymous executable region was accepted")) return 1;
    }

    // Regions become mappings, so they are page-shaped, non-empty, and do not
    // wrap.
    {
        auto bytes = well_formed();
        put64(bytes, region0 + 8U, 0ULL);
        if (!check(refused(parse_ckx(bytes), ckx_errors::region_range),
                   "an empty region was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        put64(bytes, region0 + 8U, 0x2001U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::region_range),
                   "a region that is not a whole number of pages was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        put64(bytes, region0 + 0U, code_virtual + 1U);
        if (!check(refused(parse_ckx(bytes), ckx_errors::region_range),
                   "an unaligned region address was accepted")) return 1;
    }
    {
        auto bytes = well_formed();
        put64(bytes, region0 + 0U, UINT64_MAX - ckx_page_bytes + 1ULL);
        if (!check(refused(parse_ckx(bytes), ckx_errors::region_range),
                   "a wrapping region was accepted")) return 1;
    }
    // Overlap is the classic way one signed image contains two readings of
    // itself.
    {
        auto bytes = well_formed();
        put64(bytes, region1 + 0U, code_virtual);
        if (!check(refused(parse_ckx(bytes), ckx_errors::region_overlap),
                   "two regions sharing virtual addresses were accepted")) return 1;
    }

    // The writer. It has no idea of validity of its own: it encodes, parses
    // what it encoded, and fails with whatever the parser says. So the property
    // under test is not "the writer is correct" but "the two halves cannot
    // disagree" - which is the failure every format with a reader and a writer
    // eventually has.
    {
        auto parsed = parse_ckx(well_formed());
        if (!check(static_cast<bool>(parsed), "setup parse failed")) return 1;

        std::array<std::byte, max_ckx_encoded_bytes> out{};
        auto written = build_ckx(parsed.value(), out);
        if (!check(static_cast<bool>(written), "build refused a parsed image")) return 1;
        if (!check(written.value() == ckx_encoded_bytes(2U), "wrong encoded size")) return 1;

        // Round trip: what comes back is what went in, field for field.
        auto again = parse_ckx(std::span<const std::byte>{out.data(), written.value()});
        if (!check(static_cast<bool>(again), "an image this writer produced was refused")) return 1;
        if (!check(again.value().region_count == parsed.value().region_count &&
                   again.value().entry == parsed.value().entry &&
                   again.value().authority_ceiling == parsed.value().authority_ceiling,
                   "the header did not survive a round trip")) return 1;
        for (std::size_t i = 0U; i < parsed.value().region_count; ++i) {
            if (!check(again.value().regions[i] == parsed.value().regions[i],
                       "a region did not survive a round trip")) return 1;
        }
        // Byte-for-byte, not merely equivalent. Two encodings of one plan would
        // give a package two digests for the same program.
        auto rewritten = build_ckx(again.value(), out);
        if (!check(static_cast<bool>(rewritten) && rewritten.value() == written.value(),
                   "re-encoding changed the size")) return 1;
    }
    // A buffer too small is refused rather than truncated.
    {
        auto parsed = parse_ckx(well_formed());
        std::array<std::byte, ckx_header_bytes> small{};
        if (!check(refused(build_ckx(parsed.value(), small), ckx_errors::truncated),
                   "build wrote into a buffer that could not hold the image")) return 1;
    }
    // And the writer cannot be talked into emitting something the parser would
    // reject: a plan assembled by hand with an anonymous executable region is
    // refused at build time, by the parser, without the writer knowing the rule.
    {
        CkxImage bad{};
        bad.region_count = 1U;
        bad.entry = code_virtual;
        bad.authority_ceiling = ckx_authority::unprivileged;
        bad.regions[0].virtual_address = code_virtual;
        bad.regions[0].length = ckx_page_bytes;
        bad.regions[0].permissions = CkxPermissions::read_execute;
        bad.regions[0].disclosure = CkxDisclosure::paged;
        bad.regions[0].content = CkxContent::anonymous;
        std::array<std::byte, max_ckx_encoded_bytes> out{};
        if (!check(refused(build_ckx(bad, out), ckx_errors::anonymous_executable),
                   "build emitted an image its own parser refuses")) return 1;
    }

    return 0;
}
