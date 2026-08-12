#include <os/kernel/aarch64_translation_root_sealer.hpp>
#include <os/kernel/process_translation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 8U * 4096U> memory{};
    const auto begin = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory.data()));
    EarlyPageArena arena{begin, begin + memory.size()};
    EarlyStage1Builder builder_a{arena};
    EarlyStage1Builder builder_b{arena};
    require(builder_a.initialize());
    require(builder_b.initialize());
    auto root_a = TranslationRootSealer::seal(builder_a);
    auto root_b = TranslationRootSealer::seal(builder_b);
    require(root_a && root_b);

    AddressSpaceEpochAuthority epochs{};
    auto a = epochs.acquire();
    auto b = epochs.acquire();
    require(a && b);

    ProcessTranslationTable table{};
    require(table.bind(1U, a.value(), root_a.value(), epochs));
    require(table.bind(2U, b.value(), root_b.value(), epochs));
    require(table.count() == 2U);

    auto ra = table.resolve(1U, epochs);
    auto rb = table.resolve(2U, epochs);
    require(ra && rb);
    require(ra.value().root_physical == root_a.value().root_physical());
    require(rb.value().root_physical == root_b.value().root_physical());

    const auto authority_a = ra.value().authority();
    const auto authority_b = rb.value().authority();
    require(authority_a.valid());
    require(authority_b.valid());
    require(authority_a.thread == 1U);
    require(authority_a.address_space == a.value().identity());
    require(authority_b.address_space == b.value().identity());
    require(authority_a != authority_b);

    require(!table.bind(1U, b.value(), root_b.value(), epochs));
    require(!table.bind(3U, a.value(), root_a.value(), epochs));

    auto retiring = epochs.begin_retire(a.value());
    require(retiring);
    require(!table.resolve(1U, epochs));
    require(table.resolve(2U, epochs));
    require(!table.bind(3U, a.value(), root_a.value(), epochs));

    require(table.retire(1U, a.value()));
    require(epochs.complete_retire(retiring.value()));

    // The hardware ASID may now be recycled. A new binding for the same numeric
    // thread id must nevertheless carry a different software authority identity.
    auto recycled = epochs.acquire();
    require(recycled);
    require(recycled.value().asid == a.value().asid);
    require(recycled.value().identity() != a.value().identity());
    require(table.bind(1U, recycled.value(), root_a.value(), epochs));
    auto rebound = table.resolve(1U, epochs);
    require(rebound);
    require(rebound.value().authority().thread == authority_a.thread);
    require(rebound.value().authority().address_space != authority_a.address_space);
    require(rebound.value().authority() != authority_a);

    return 0;
}
