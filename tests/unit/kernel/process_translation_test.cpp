#include <os/kernel/process_translation.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    os::kernel::AddressSpaceEpochAuthority epochs{};
    auto a = epochs.acquire();
    auto b = epochs.acquire();
    require(a && b);

    os::kernel::ProcessTranslationTable table{};
    require(table.bind(1U, a.value(), 0x1000ULL, epochs));
    require(table.bind(2U, b.value(), 0x2000ULL, epochs));
    require(table.count() == 2U);

    auto ra = table.resolve(1U, epochs);
    auto rb = table.resolve(2U, epochs);
    require(ra && rb);
    require(ra.value().epoch == a.value());
    require(rb.value().epoch == b.value());
    require(ra.value().root_physical == 0x1000ULL);
    require(rb.value().root_physical == 0x2000ULL);

    require(!table.bind(1U, b.value(), 0x3000ULL, epochs));
    require(!table.bind(3U, a.value(), 0x3000ULL, epochs));

    auto retiring = epochs.begin_retire(a.value());
    require(retiring);
    require(!table.resolve(1U, epochs));
    require(table.resolve(2U, epochs));

    // A stale/retiring epoch cannot be inserted under a new thread id. Cookie
    // revokes translation authority at bind time as well as resolve time.
    require(!table.bind(3U, a.value(), 0x3000ULL, epochs));

    require(!table.retire(1U, b.value()));
    require(table.retire(1U, a.value()));
    require(table.count() == 1U);
    require(!table.resolve(1U, epochs));

    require(epochs.complete_retire(retiring.value()));
    auto a2 = epochs.acquire();
    require(a2);
    require(a2.value().generation != a.value().generation || a2.value().asid != a.value().asid);

    // Reuse requires the fresh epoch token; the old generation stays dead.
    require(!table.bind(3U, a.value(), 0x3000ULL, epochs));
    require(table.bind(3U, a2.value(), 0x3000ULL, epochs));

    return 0;
}
