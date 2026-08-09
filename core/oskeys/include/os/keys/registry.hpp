#pragma once

#include <array>
#include <cstddef>

#include <os/core/result.hpp>
#include <os/keys/key.hpp>
#include <os/keys/provider.hpp>

namespace os::keys {

inline constexpr std::size_t max_key_records = 128U;

struct KeyRecord final {
    bool occupied {false};
    bool destroyed {false};
    KeyOwner owner {};
    KeyDescriptor descriptor {};
    ProviderKeyReference provider_key {};
};

// Fixed-capacity metadata registry. Public KeyId values are locators, not
// authority: every lookup is still checked against the trusted caller owner.
// Destroyed records remain tombstones so a logical KeyId cannot be silently
// reused within this registry generation.
class KeyRegistry final {
public:
    explicit KeyRegistry(KeyProvider& provider) noexcept : provider_(&provider) {}

    [[nodiscard]] os::core::Result<KeyDescriptor>
    create(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights) noexcept;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    provider_reference(KeyOwner caller, KeyId id, RightsMask required_right) const noexcept;

    [[nodiscard]] os::core::Result<void>
    destroy(KeyOwner caller, KeyId id) noexcept;

    [[nodiscard]] std::size_t record_count() const noexcept;
    [[nodiscard]] std::size_t active_count() const noexcept;

private:
    [[nodiscard]] KeyRecord* find(KeyId id) noexcept;
    [[nodiscard]] const KeyRecord* find(KeyId id) const noexcept;

    KeyProvider* provider_ {nullptr};
    std::array<KeyRecord, max_key_records> records_ {};
};

} // namespace os::keys
