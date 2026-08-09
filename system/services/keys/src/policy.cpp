#include <os/keys/policy.hpp>

#include <os/core/identity.hpp>
#include <os/keys/error.hpp>

namespace os::keys {

os::core::Result<void>
ApplicationKeyPolicy::enable(KeyOwner owner) noexcept {
    if (!os::core::valid_principal(owner.principal)) {
        return key_error(errors::invalid_key);
    }

    for (const auto& entry : entries_) {
        if (entry.occupied && entry.owner == owner) return {};
        if (entry.occupied && entry.owner.principal == owner.principal &&
            entry.owner.user != owner.user) {
            return key_error(errors::hierarchy_conflict);
        }
    }

    for (auto& entry : entries_) {
        if (!entry.occupied) {
            entry = Entry{.occupied = true, .owner = owner};
            return {};
        }
    }
    return key_error(errors::policy_capacity);
}

os::core::Result<void>
ApplicationKeyPolicy::disable(KeyOwner owner) noexcept {
    if (!os::core::valid_principal(owner.principal)) {
        return key_error(errors::invalid_key);
    }
    for (auto& entry : entries_) {
        if (entry.occupied && entry.owner == owner) {
            entry = Entry{};
            return {};
        }
    }
    return key_error(errors::policy_not_registered);
}

bool ApplicationKeyPolicy::enabled(KeyOwner owner) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.owner == owner) return true;
    }
    return false;
}

std::size_t ApplicationKeyPolicy::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& entry : entries_) {
        if (entry.occupied) ++count;
    }
    return count;
}

} // namespace os::keys
