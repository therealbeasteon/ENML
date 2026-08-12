#pragma once

#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel {

// Context-bound software authority for one executing thread. The address-space
// component intentionally contains no ASID. ASIDs may be recycled after TLB
// retirement; slot+generation names the software lifetime that security policy
// is allowed to trust.
struct ExecutionAuthority final {
    ThreadId thread {invalid_thread};
    AddressSpaceIdentity address_space {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return thread != invalid_thread && address_space.valid();
    }

    [[nodiscard]] friend constexpr bool operator==(
        const ExecutionAuthority&, const ExecutionAuthority&) = default;
};

} // namespace os::kernel
