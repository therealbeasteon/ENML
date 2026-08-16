#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <os/core/result.hpp>

// The Cookie executable image: `.ckx`.
//
// **A `.ckx` is not a file to be loaded. It is a plan for an address space to
// be constructed.** That is the whole difference from every format it will be
// compared to, and it is not a stylistic one: Cookie has no `load` operation
// and never will. docs/M7_11_MEMORY.md's kernel offers create-a-space,
// donate-pages-to-it, map, seal, admit-a-thread - and nothing that takes a file.
// A format describing where bytes sit in a file would be describing an
// operation this system does not have.
//
// So the regions below carry no file offsets. They say *what a region's content
// is*, not *where its bytes are*, and three properties follow that ELF, PE and
// Mach-O cannot have because they describe files:
//
//   1. Content is addressed by digest, so the bytes may come from anywhere - a
//      package, a cache, a peer, a pager answering a fault. The image does not
//      know and does not need to.
//   2. Two regions in two different applications with the same digest provably
//      hold the same content, so physical sharing is a *consequence of
//      identity* rather than a decision someone has to be trusted to make
//      correctly about a path or a library name.
//   3. The cost of constructing the space is computed from the plan rather than
//      declared in it, so an image cannot lie about what it will take to build.
//      Cookie needs this and other systems do not, because Cookie's no-allocator
//      decision makes the *caller* supply every page - and a loader that
//      discovers the cost halfway through has already half-built a space.
//
// `.cookie` is the application package that contains one or more of these plus
// the manifest. Identity and signature live there; a `.ckx` deliberately cannot
// certify itself - see docs/M7_12_CKX_FORMAT.md.
//
// Nothing here is reachable from the kernel. docs/M7_12_FIRST_PROGRAM.md decided
// that nothing in the trusted image interprets attacker-supplied program bytes,
// and this parser living in os::image rather than os::kernel is how that stays
// checkable: docs/M7_10_LINE_COUNT.md is where its absence is a number.
namespace os::image {

// Fixed sizes, all of them. Nothing is derived from the input, because the
// no-allocator discipline the kernel adopted is the right one for a parser over
// untrusted bytes too: a count above the maximum is a refusal, never a resize.
inline constexpr std::size_t ckx_magic_bytes = 4U;
inline constexpr std::size_t ckx_header_bytes = 32U;
inline constexpr std::size_t ckx_region_bytes = 56U;
inline constexpr std::size_t ckx_digest_bytes = 32U;
inline constexpr std::size_t max_ckx_regions = 16U;
inline constexpr std::uint16_t ckx_format_version_1 = 1U;

// Regions are described in pages because they become mappings, and Cookie maps
// pages. Matching the kernel's granule rather than inventing a second unit is
// deliberate: a format whose alignment disagreed with the machine's would push
// the disagreement into the loader, which is the one component that must not
// have to reconcile two answers.
inline constexpr std::uint64_t ckx_page_bytes = 4096ULL;

inline constexpr std::array<std::byte, ckx_magic_bytes> ckx_magic{
    std::byte{'C'}, std::byte{'K'}, std::byte{'X'}, std::byte{0}};

namespace ckx_errors {
inline constexpr std::uint32_t truncated = 1U;
inline constexpr std::uint32_t bad_magic = 2U;
inline constexpr std::uint32_t unsupported_version = 3U;
inline constexpr std::uint32_t reserved_not_zero = 4U;
inline constexpr std::uint32_t region_count = 5U;
inline constexpr std::uint32_t region_range = 6U;
inline constexpr std::uint32_t region_overlap = 7U;
inline constexpr std::uint32_t invalid_entry = 8U;
inline constexpr std::uint32_t invalid_permissions = 9U;
inline constexpr std::uint32_t invalid_disclosure = 10U;
inline constexpr std::uint32_t invalid_authority = 11U;
inline constexpr std::uint32_t entry_not_executable = 12U;
inline constexpr std::uint32_t invalid_content = 13U;
inline constexpr std::uint32_t anonymous_executable = 14U;
} // namespace ckx_errors

// Deliberately the same three the machine layer already has. A fourth value
// would be a permission the kernel cannot express, which is a promise the
// format could not keep.
enum class CkxPermissions : std::uint8_t {
    read = 1U,
    read_write = 2U,
    read_execute = 3U,
};

// Deliberately the same two docs/M7_11_FAULT_PRIVACY.md already defines.
//
// Declaring it in the image is the novel part: a region holding key material
// says `sealed` in bytes the package digest covers, so a compromised loader
// cannot downgrade it to `paged` to make its faults observable to a pager. No
// other executable format can express this because no other system reports
// faults by region instead of by address.
enum class CkxDisclosure : std::uint8_t {
    paged = 1U,
    sealed = 2U,
};

// Where a region's content comes from - and never *where in a file*.
enum class CkxContent : std::uint8_t {
    // Zero-filled. Stack, heap, bss. The digest field must be all zero, because
    // a digest naming content that is by definition absent is a contradiction
    // the parser should refuse rather than ignore.
    anonymous = 1U,
    // Named by digest. The loader may obtain the bytes anywhere; what it may
    // not do is supply bytes that hash to something else.
    named = 2U,
};

// The most this image may ever hold, as a bitmask over the kernel's coarse
// authority classes.
//
// The inversion is the point. An .apk manifest fixes what an app *asks for*;
// what it ends up holding is a runtime question answered by whoever grants.
// Here the ceiling is signed content the granting path does not hold, so a
// compromised process manager cannot widen a program beyond what its author
// signed. It is also answerable without running anything.
namespace ckx_authority {
inline constexpr std::uint64_t unprivileged = 1ULL << 0U;
inline constexpr std::uint64_t process_control = 1ULL << 1U;
inline constexpr std::uint64_t memory_control = 1ULL << 2U;
inline constexpr std::uint64_t interrupt_control = 1ULL << 3U;
inline constexpr std::uint64_t capability_control = 1ULL << 4U;
inline constexpr std::uint64_t all =
    unprivileged | process_control | memory_control | interrupt_control |
    capability_control;
} // namespace ckx_authority

struct CkxRegion final {
    std::uint64_t virtual_address {0ULL};
    std::uint64_t length {0ULL};
    CkxPermissions permissions {CkxPermissions::read};
    CkxDisclosure disclosure {CkxDisclosure::paged};
    CkxContent content {CkxContent::anonymous};
    std::array<std::byte, ckx_digest_bytes> digest {};

    [[nodiscard]] constexpr std::uint64_t pages() const noexcept {
        return length / ckx_page_bytes;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const CkxRegion&, const CkxRegion&) = default;
};

struct CkxImage final {
    std::uint16_t format_version {0U};
    std::uint64_t entry {0ULL};
    std::uint64_t authority_ceiling {0ULL};
    std::array<CkxRegion, max_ckx_regions> regions {};
    std::size_t region_count {0U};

    [[nodiscard]] constexpr std::span<const CkxRegion> used() const noexcept {
        return std::span<const CkxRegion>{regions.data(), region_count};
    }
};

// What building this space will cost the caller, in pages it must already hold.
//
// This exists because of a decision no other system made. Cookie's kernel has no
// allocator: docs/M7_11_MEMORY.md settled that every kernel object comes from
// memory a process already holds authority over, so a loader must *donate* the
// translation tables before it can map anything. A loader that learns the cost
// as it goes fails halfway and leaves a partly built space; one that knows it
// first can refuse before it starts, which is the difference between an error a
// caller can act on and a mess it has to clean up.
//
// Computed from the region table rather than declared in the header, and that
// distinction is the security content: a declared cost is a number an image can
// lie about, and the lie would be discovered as an exhaustion partway through
// construction - exactly the failure the field would have existed to prevent.
struct CkxConstructionCost final {
    // Pages of content and anonymous memory the regions occupy.
    std::uint64_t backing_pages {0ULL};
    // Pages of AArch64 stage-1 translation tables the region set needs,
    // including the root. Architecture-specific by nature, which is why it is
    // computed by a named-for-the-architecture function rather than stored in
    // an architecture-neutral format.
    std::uint64_t table_pages {0ULL};

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return backing_pages + table_pages;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const CkxConstructionCost&, const CkxConstructionCost&) = default;
};

[[nodiscard]] CkxConstructionCost aarch64_construction_cost(const CkxImage& image) noexcept;

// Parses and validates a `.ckx` over bytes that are assumed hostile.
//
// The rules are the format's entire security content, so they are listed rather
// than left to be inferred:
//
//   * an unknown version is refused, never treated as the newest understood
//     one - a format that degrades gracefully is one an attacker picks the
//     version of;
//   * a non-zero reserved field is refused: a reserved field that is ignored is
//     a covert channel through signed content;
//   * regions are page-aligned, non-empty, and may not wrap;
//   * regions may not overlap - overlap is the classic way one signed image
//     contains two readings of itself;
//   * an `anonymous` region must carry a zero digest and a `named` region must
//     not, because each of those is a claim about content and the other value
//     contradicts it;
//   * **an executable region may never be `anonymous`.** "Execute whatever
//     happens to be there" is not expressible in a Cookie program, and this is
//     the rule that makes that true of the format rather than of a convention;
//   * the entry is four-byte aligned and lands inside a region this image
//     itself makes executable;
//   * nothing is sized by the input.
//
// It never dereferences the image's virtual addresses. Constructing the space
// is the loader's job and mapping it is the kernel's.
[[nodiscard]] os::core::Result<CkxImage> parse_ckx(
    std::span<const std::byte> bytes) noexcept;

} // namespace os::image
