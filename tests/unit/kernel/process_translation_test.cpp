#include <os/kernel/aarch64_translation_root_sealer.hpp>
#include <os/kernel/capability.hpp>
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

    // M7.8.1: process-facing capabilities bind to the exact software execution
    // lifetime. The compatibility ThreadId API must not authorize a bound slot.
    CapabilityTable capabilities{};
    auto authority_root = capabilities.mint(authority_a, 0xA001U, all_rights, true);
    require(authority_root);
    require(capabilities.holds(authority_a, authority_root.value()));
    require(!capabilities.holds(authority_a.thread, authority_root.value()));
    auto child = capabilities.grant(
        authority_a, authority_root.value(), authority_b, 0x0FU, false);
    require(child);
    require(capabilities.holds(authority_b, child.value()));

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
    const auto recycled_authority = rebound.value().authority();
    require(recycled_authority.thread == authority_a.thread);
    require(recycled_authority.address_space != authority_a.address_space);
    require(recycled_authority != authority_a);

    // Same ThreadId and even the same recycled ASID do not recover stale
    // capability authority. Exact slot+generation is load-bearing here.
    require(!capabilities.holds(recycled_authority, authority_root.value()));
    require(!capabilities.grant(
        recycled_authority, authority_root.value(), authority_b, 0x01U, false));
    require(!capabilities.revoke(recycled_authority, child.value()));

    auto root_info = capabilities.describe(authority_root.value());
    require(root_info);
    require(root_info.value().context_bound);
    require(root_info.value().holder_authority() == authority_a);

    // Administrative thread teardown remains deliberately stronger than a
    // process claim and removes the old bound root plus its derivation subtree.
    require(capabilities.revoke_all_held_by(1U) == 2U);
    require(capabilities.live_capability_count() == 0U);

    return 0;
}