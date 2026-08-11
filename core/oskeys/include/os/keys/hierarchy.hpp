#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/keys/key.hpp>
#include <os/keys/provider.hpp>

namespace os::keys {

inline constexpr std::size_t max_profile_roots = 16U;
inline constexpr std::size_t max_application_roots = 64U;

enum class KeyProtectionScope : std::uint32_t {
    system = 1U,
    user_profile = 2U,
    application = 3U,
};

[[nodiscard]] constexpr bool valid_protection_scope(KeyProtectionScope scope) noexcept {
    switch (scope) {
    case KeyProtectionScope::system:
    case KeyProtectionScope::user_profile:
    case KeyProtectionScope::application:
        return true;
    }
    return false;
}

struct KeyProtectionBinding final {
    KeyProtectionScope scope {KeyProtectionScope::application};
    KeyOwner owner {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return valid_protection_scope(scope) &&
            (owner.principal.high != 0U || owner.principal.low != 0U);
    }

    [[nodiscard]] friend constexpr auto operator<=>(
        const KeyProtectionBinding&,
        const KeyProtectionBinding&) = default;
};

[[nodiscard]] constexpr bool valid_hierarchy_edge(
    const KeyProtectionBinding& parent,
    const KeyProtectionBinding& child) noexcept {
    if (!parent.valid() || !child.valid()) return false;

    if (parent.scope == KeyProtectionScope::system &&
        child.scope == KeyProtectionScope::user_profile) {
        return true;
    }
    if (parent.scope == KeyProtectionScope::user_profile &&
        child.scope == KeyProtectionScope::application) {
        return parent.owner.user == child.owner.user;
    }
    return false;
}

struct RootKeyReference final {
    std::uint64_t value {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] friend constexpr auto operator<=>(
        const RootKeyReference&,
        const RootKeyReference&) = default;
};

enum class RootErasureAssurance : std::uint8_t {
    logical_only = 1U,
    effaceable_key_storage = 2U,
};

struct ProfileRootErasureReport final {
    std::size_t application_roots_destroyed {0U};
    RootErasureAssurance assurance {RootErasureAssurance::logical_only};

    [[nodiscard]] friend constexpr bool operator==(
        const ProfileRootErasureReport&,
        const ProfileRootErasureReport&) = default;
};

class HierarchicalKeyProvider : public PersistentKeyProvider {
public:
    ~HierarchicalKeyProvider() override = default;

    [[nodiscard]] virtual os::core::Result<RootKeyReference>
    acquire_system_root(KeyProtectionBinding system_binding) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<RootKeyReference>
    acquire_child_root(
        RootKeyReference parent,
        KeyProtectionBinding parent_binding,
        KeyProtectionBinding child_binding) noexcept = 0;

    // Purpose and scope are jointly authoritative. Production providers must
    // reject application_data_aead at profile scope and profile_storage_aead at
    // application scope even if a higher layer accidentally asks for it.
    [[nodiscard]] virtual os::core::Result<ProviderKeyReference>
    generate_under_root(
        RootKeyReference root,
        KeyProtectionBinding binding,
        KeyPurpose purpose) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    destroy_root(RootKeyReference root, KeyProtectionBinding binding) noexcept = 0;

    [[nodiscard]] virtual RootErasureAssurance root_erasure_assurance() const noexcept {
        return RootErasureAssurance::logical_only;
    }
};

class KeyHierarchy final {
public:
    explicit KeyHierarchy(HierarchicalKeyProvider& provider) noexcept : provider_(&provider) {}

    [[nodiscard]] os::core::Result<void>
    initialize(KeyProtectionBinding system_binding) noexcept;

    [[nodiscard]] os::core::Result<void>
    ensure_profile(KeyProtectionBinding profile_binding) noexcept;

    [[nodiscard]] os::core::Result<void>
    ensure_application(KeyProtectionBinding application_binding) noexcept;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    generate_application_data_key(
        KeyProtectionBinding application_binding,
        KeyPurpose purpose) noexcept;

    // system.storage obtains an opaque bulk-data key directly beneath the
    // selected profile root. Applications never receive this authority. The
    // profile root therefore remains the single cryptographic cut point for
    // duress erasure while app-specific keys remain independently scoped.
    [[nodiscard]] os::core::Result<ProviderKeyReference>
    generate_profile_storage_key(os::core::UserId user) noexcept;

    [[nodiscard]] os::core::Result<ProfileRootErasureReport>
    destroy_profile(os::core::UserId user) noexcept;

    [[nodiscard]] bool initialized() const noexcept { return system_.occupied; }
    [[nodiscard]] std::size_t profile_count() const noexcept;
    [[nodiscard]] std::size_t application_count() const noexcept;

private:
    struct RootSlot final {
        bool occupied {false};
        KeyProtectionBinding binding {};
        RootKeyReference reference {};
    };

    [[nodiscard]] RootSlot* find_profile(os::core::UserId user) noexcept;
    [[nodiscard]] const RootSlot* find_profile(os::core::UserId user) const noexcept;
    [[nodiscard]] RootSlot* find_application(KeyProtectionBinding binding) noexcept;
    [[nodiscard]] const RootSlot* find_application(KeyProtectionBinding binding) const noexcept;

    HierarchicalKeyProvider* provider_ {nullptr};
    RootSlot system_ {};
    std::array<RootSlot, max_profile_roots> profiles_ {};
    std::array<RootSlot, max_application_roots> applications_ {};
};

struct SecurityEpoch final {
    std::uint64_t value {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] constexpr bool can_advance() const noexcept {
        return valid() && value != std::numeric_limits<std::uint64_t>::max();
    }
    [[nodiscard]] friend constexpr auto operator<=>(const SecurityEpoch&, const SecurityEpoch&) = default;
};

class MonotonicSecurityState {
public:
    virtual ~MonotonicSecurityState() = default;

    [[nodiscard]] virtual os::core::Result<SecurityEpoch>
    current() noexcept = 0;

    [[nodiscard]] virtual os::core::Result<SecurityEpoch>
    advance(SecurityEpoch expected_current) noexcept = 0;
};

} // namespace os::keys
