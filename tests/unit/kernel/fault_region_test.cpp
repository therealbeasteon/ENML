#include <os/kernel/fault_region.hpp>

#include <cstdlib>

namespace {
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;

    constexpr std::uint64_t heap_base = 0x0000'0000'2000'0000ULL;
    constexpr std::uint64_t heap_length = 0x0010'0000ULL; // 1 MiB, 256 pages
    constexpr std::uint64_t keys_base = 0x0000'0000'3000'0000ULL;

    FaultRegionTable regions{};
    auto heap = regions.declare(heap_base, heap_length, FaultDisclosure::paged);
    require(static_cast<bool>(heap));
    auto keys = regions.declare(keys_base, 4096ULL, FaultDisclosure::sealed);
    require(static_cast<bool>(keys));
    require(regions.count() == 2U);

    // Overlap is refused: an address resolving to two regions would make the
    // report depend on search order.
    auto straddle = regions.declare(
        heap_base + 4096ULL, 8192ULL, FaultDisclosure::paged);
    require(!straddle);
    require(straddle.error().code == fault_region_errors::overlaps);

    // --------------------------------------------------------------------
    // The report names a region, never an address.
    // --------------------------------------------------------------------
    auto first = regions.resolve(heap_base + 0x7'F000ULL, false);
    require(first.deliverable());
    require(first.region == heap.value());

    // The whole attack, expressed as a test. A pager that has been told once
    // tries to learn the access trace by being told again - here at 255 further
    // distinct page offsets inside the same region, which is exactly the
    // resolution a controlled-channel attack wants. It learns nothing: the
    // region announced its one transition already.
    for (std::uint64_t page = 0ULL; page < heap_length / 4096ULL; ++page) {
        auto probe = regions.resolve(heap_base + page * 4096ULL, page % 2U == 0U);
        require(!probe.deliverable());
    }

    // Backing arrives, and it is the kernel that records it.
    require(static_cast<bool>(
        regions.mark_backed(heap.value(), FaultRegionState::backed_shared)));
    auto backed = regions.state(heap.value());
    require(static_cast<bool>(backed));
    require(backed.value() == FaultRegionState::backed_shared);

    // A pager cannot hand the region back to the faulting state. This is the
    // re-arm primitive, and there is no interface for it.
    auto rearm = regions.mark_backed(heap.value(), FaultRegionState::unbacked);
    require(!rearm);
    require(rearm.error().code == fault_region_errors::wrong_state);

    // A read of backed-shared memory is not a demand for memory.
    auto shared_read = regions.resolve(heap_base + 0x1000ULL, false);
    require(!shared_read.deliverable());

    // The second and last transition: a write to shared backing. Announced
    // once...
    auto private_write = regions.resolve(heap_base + 0x1000ULL, true);
    require(private_write.deliverable());
    require(private_write.region == heap.value());
    require(private_write.write);

    // ...and never again, at any offset.
    auto probe_again = regions.resolve(heap_base + 0x2000ULL, true);
    require(!probe_again.deliverable());
    auto probe_third = regions.resolve(heap_base + 0x3000ULL, true);
    require(!probe_third.deliverable());

    require(static_cast<bool>(
        regions.mark_backed(heap.value(), FaultRegionState::backed_private)));
    auto after_private = regions.resolve(heap_base + 0x4000ULL, true);
    require(!after_private.deliverable());

    // --------------------------------------------------------------------
    // sealed: no fault in it is ever a question userland is asked.
    // --------------------------------------------------------------------
    auto sealed_read = regions.resolve(keys_base, false);
    require(!sealed_read.deliverable());
    require(sealed_read.region == invalid_fault_region);
    auto sealed_write = regions.resolve(keys_base + 8ULL, true);
    require(!sealed_write.deliverable());

    // A sealed region never announces, so it cannot be drained by probing it
    // before the process runs.
    for (int attempt = 0; attempt < 64; ++attempt) {
        auto probe = regions.resolve(keys_base + 16ULL, attempt % 2 == 0);
        require(!probe.deliverable());
    }

    // --------------------------------------------------------------------
    // Undeclared memory is nobody's question.
    // --------------------------------------------------------------------
    auto stray = regions.resolve(0x0000'0000'9000'0000ULL, true);
    require(!stray.deliverable());
    require(stray.region == invalid_fault_region);
    require(stray.disposition == FaultDisposition::terminate);

    // Boundaries: last byte in, first byte out.
    FaultRegionTable edges{};
    auto one = edges.declare(0x1000ULL, 0x1000ULL, FaultDisclosure::paged);
    require(static_cast<bool>(one));
    require(edges.resolve(0x1FFFULL, false).deliverable());
    require(!edges.resolve(0x2000ULL, false).deliverable());

    // Identifiers are not reused, so a report cannot be misattributed to a
    // region that has since been replaced.
    FaultRegionTable ids{};
    auto a = ids.declare(0x1000ULL, 0x1000ULL, FaultDisclosure::paged);
    auto b = ids.declare(0x2000ULL, 0x1000ULL, FaultDisclosure::paged);
    require(static_cast<bool>(a));
    require(static_cast<bool>(b));
    require(a.value() != b.value());
    require(a.value() != invalid_fault_region);

    // Unaligned and empty declarations are refused.
    require(!ids.declare(0x1001ULL + 0x10000ULL, 0x1000ULL, FaultDisclosure::paged));
    require(!ids.declare(0x40000ULL, 0ULL, FaultDisclosure::paged));

    return 0;
}
