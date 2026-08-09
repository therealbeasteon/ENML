#pragma once

#include <array>
#include <cstddef>

#include <os/core/result.hpp>
#include <os/keys/key.hpp>

namespace os::keys {

inline constexpr std::size_t max_application_key_policies = 64U;

// Trusted service-local admission policy. KeyOwner values are published only by
// system control code; public application requests never provide this identity.
// A policy entry permits that durable PrincipalId+UserId to acquire/open Key
// Service capabilities. Removing it is an authority revocation, not key/data
// destruction.
class ApplicationKeyPolicy final {
public:
    [[nodiscard]] os::core::Result<void>
    enable(KeyOwner owner) noexcept;

    [[nodiscard]] os::core::Result<void>
    disable(KeyOwner owner) noexcept;

    [[nodiscard]] bool enabled(KeyOwner owner) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry final {
        bool occupied {false};
        KeyOwner owner {};
    };

    std::array<Entry, max_application_key_policies> entries_ {};
};

} // namespace os::keys
