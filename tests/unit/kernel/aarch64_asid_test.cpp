#include <os/kernel/aarch64_asid.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    constexpr std::uint64_t root = 0x0000'0000'4000'0000ULL;
    constexpr AddressSpaceEpoch epoch{.slot = 1U, .generation = 7U, .asid = 5U};

    static_assert(supported_initial_asid(1U));
    static_assert(supported_initial_asid(255U));
    static_assert(!supported_initial_asid(0U));

    const auto ttbr = ttbr0_el1_value(root, epoch.asid);
    require(ttbr == (root | (5ULL << 48U)));
    require((ttbr & page_address_mask) == root);
    require((ttbr >> 48U) == 5ULL);

    require(aside1is_operand(epoch.asid) == (5ULL << 48U));
    require(aside1is_operand(0U) == 0ULL);
    require(ttbr0_el1_value(root + 1ULL, epoch.asid) == 0ULL);

    return 0;
}
