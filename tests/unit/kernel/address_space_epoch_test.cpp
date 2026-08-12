#include <os/kernel/address_space_epoch.hpp>

#include <array>
#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;

    AddressSpaceEpochAuthority authority{};
    auto first = authority.acquire();
    require(static_cast<bool>(first));
    require(first.value().valid());
    require(first.value().identity().valid());
    require(first.value().asid != kernel_reserved_asid);
    require(authority.active(first.value()));

    const auto first_identity = first.value().identity();
    require(first_identity.slot == first.value().slot);
    require(first_identity.generation == first.value().generation);

    auto retiring = authority.begin_retire(first.value());
    require(static_cast<bool>(retiring));
    require(!authority.active(first.value()));
    require(authority.retiring(retiring.value()));
    require(authority.active_count() == 0U);
    require(authority.retiring_count() == 1U);

    // A quarantined ASID cannot be reused before TLB retirement is explicitly
    // completed; acquire must move to a different slot/ASID.
    auto while_quarantined = authority.acquire();
    require(static_cast<bool>(while_quarantined));
    require(while_quarantined.value().asid != first.value().asid);

    require(static_cast<bool>(authority.complete_retire(retiring.value())));
    require(authority.retiring_count() == 0U);

    auto reused = authority.acquire();
    require(static_cast<bool>(reused));
    require(reused.value().asid == first.value().asid);
    require(reused.value().generation != first.value().generation);
    require(reused.value().identity() != first_identity);
    require(!authority.active(first.value()));
    require(authority.active(reused.value()));

    // Hardware identity is deliberately reusable; software authority is not.
    require(reused.value().asid == first.value().asid);
    require(reused.value().identity().slot == first_identity.slot);
    require(reused.value().identity().generation != first_identity.generation);

    // Old retirement tickets cannot retire a new incarnation of the same ASID.
    require(!authority.complete_retire(retiring.value()));

    AddressSpaceEpochAuthority bounded{};
    std::array<AddressSpaceEpoch, max_address_space_epochs> epochs{};
    for (std::size_t i = 0U; i < epochs.size(); ++i) {
        auto epoch = bounded.acquire();
        require(static_cast<bool>(epoch));
        epochs[i] = epoch.value();
    }
    require(bounded.active_count() == max_address_space_epochs);
    require(!bounded.acquire());

    return 0;
}
