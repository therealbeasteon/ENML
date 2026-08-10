#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/package/package.hpp>

namespace os::app {

// This is the maximum live-application lifecycle set published by App Manager.
// Product shell state is not allowed to grow a second, larger hidden registry.
inline constexpr std::size_t max_application_lifecycle_instances = 16U;

// Shell/product consumers need semantic application identity and exact live
// process identity, not native PIDs, executable paths, storage roots or package
// filesystem handles. App Manager remains the authority that creates this data.
struct ApplicationLifecycleRecord final {
    os::core::ApplicationInstanceId instance {};
    os::package::ApplicationIdentity application {};
    os::core::PeerIdentity identity {};

    [[nodiscard]] bool valid() const noexcept {
        return instance.value() != 0U && application.valid() &&
            os::core::valid_peer_identity(identity);
    }

    [[nodiscard]] friend bool operator==(
        const ApplicationLifecycleRecord&,
        const ApplicationLifecycleRecord&) = default;
};

struct ApplicationLifecycleSnapshot final {
    // Monotonic within one App Manager lifetime. Revision zero is reserved as
    // invalid so a future authenticated transport can reject malformed state.
    std::uint64_t revision {0U};
    std::array<ApplicationLifecycleRecord, max_application_lifecycle_instances>
        applications {};
    std::size_t count {0U};
};

} // namespace os::app
