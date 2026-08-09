#pragma once

#include <compare>
#include <cstdint>
#include <limits>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/keys/key.hpp>
#include <os/keys/provider.hpp>

namespace os::keys {

// Logical protection scope is trusted system metadata. Public callers never
// choose another principal's scope or owner through a Key Service payload.
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

// Only downward hierarchy transitions are valid. The system root can parent a
// user/profile root. A profile root can parent application roots only for the
// same durable UserId. Application roots are leaves with respect to root-key
// delegation and can only mint data keys through the provider.
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

// Provider-private root reference. Like ProviderKeyReference, this is an
// ephemeral process-local handle, not durable identity and never public ABI.
struct RootKeyReference final {
    std::uint64_t value {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] friend constexpr auto operator<=>(
        const RootKeyReference&,
        const RootKeyReference&) = default;
};

// A production implementation may map these operations to a TPM/TEE/HSM. The
// interface deliberately has no method that exports raw root-key bytes.
//
// `acquire_system_root` establishes the hardware/provider-owned root for the
// trusted system binding. `create_child_root` is called only after core policy
// has validated a system->profile or profile->application edge. Data-key
// generation occurs beneath the selected root and returns the existing opaque
// ProviderKeyReference consumed by KeyRegistry/KeyProvider operations.
class HierarchicalKeyProvider : public PersistentKeyProvider {
public:
    ~HierarchicalKeyProvider() override = default;

    [[nodiscard]] virtual os::core::Result<RootKeyReference>
    acquire_system_root(KeyProtectionBinding system_binding) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<RootKeyReference>
    create_child_root(
        RootKeyReference parent,
        KeyProtectionBinding parent_binding,
        KeyProtectionBinding child_binding) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<ProviderKeyReference>
    generate_under_root(
        RootKeyReference root,
        KeyProtectionBinding binding,
        KeyPurpose purpose) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    destroy_root(RootKeyReference root, KeyProtectionBinding binding) noexcept = 0;
};

struct SecurityEpoch final {
    std::uint64_t value {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] constexpr bool can_advance() const noexcept {
        return valid() && value != std::numeric_limits<std::uint64_t>::max();
    }
    [[nodiscard]] friend constexpr auto operator<=>(const SecurityEpoch&, const SecurityEpoch&) = default;
};

// Contract for a production monotonic anti-rollback source. `advance` must be
// atomic with respect to the provider's own state: it succeeds only if the
// current value equals `expected_current`, then returns the strictly next epoch.
//
// M2.7 defines this boundary but does not pretend an ordinary host file can
// provide rollback resistance. Integrating this contract with KRG snapshots
// requires a reviewed crash-consistent protocol in a later slice.
class MonotonicSecurityState {
public:
    virtual ~MonotonicSecurityState() = default;

    [[nodiscard]] virtual os::core::Result<SecurityEpoch>
    current() noexcept = 0;

    [[nodiscard]] virtual os::core::Result<SecurityEpoch>
    advance(SecurityEpoch expected_current) noexcept = 0;
};

} // namespace os::keys
