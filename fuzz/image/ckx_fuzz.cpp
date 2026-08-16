#include <cstddef>
#include <cstdint>
#include <span>

#include <os/image/ckx.hpp>

// The .ckx parser, over bytes chosen by whoever supplied the image.
//
// This target ships in the same change as the parser rather than after it, and
// that ordering is deliberate: docs/SECURITY_AUDIT_2026_08_16.md found the
// kernel's corpus had stopped tracking its own decoders, and the fix for that
// is not to catch up again later but to make a parser and its fuzzer one
// change. This is the first structured, attacker-supplied format Cookie parses
// anywhere - docs/M4_5_FUZZING_DEPTH.md is the reason it gets one on arrival.
//
// The parser allocates nothing and reads nothing outside the span, so the
// property being fuzzed is exactly that: no input reaches a byte it was not
// given, and no input is accepted that the rules in docs/M7_12_CKX_FORMAT.md
// say must be refused.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto image = os::image::parse_ckx(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size});
    if (!image) return 0;

    // An accepted image has to be self-consistent, and these are the invariants
    // the loader will rely on without re-checking. Asserting them here is what
    // makes "the parser accepted it" mean something under the sanitizers.
    const auto& value = image.value();
    if (value.region_count == 0U || value.region_count > os::image::max_ckx_regions) {
        __builtin_trap();
    }
    for (const auto& region : value.used()) {
        if (region.length == 0U) __builtin_trap();
        if (region.length % os::image::ckx_page_bytes != 0U) __builtin_trap();
        if (region.virtual_address % os::image::ckx_page_bytes != 0U) __builtin_trap();
        // The rule with no equivalent in any other format: nothing executable
        // is anonymous. If this ever trips, an image has described memory that
        // would be fetched as instructions without naming what those
        // instructions are.
        if (region.permissions == os::image::CkxPermissions::read_execute &&
            region.content != os::image::CkxContent::named) {
            __builtin_trap();
        }
    }
    // The cost function runs on every accepted image, because it is arithmetic
    // over attacker-influenced lengths and addresses and is exactly the shape
    // that overflows quietly. The sanitizers are the assertion.
    (void)os::image::aarch64_construction_cost(value);
    return 0;
}
