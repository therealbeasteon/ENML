#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/storage/path.hpp>
#include <os/storage/protected_atomic_replace.hpp>

namespace os::storage {

inline constexpr std::size_t max_protected_namespace_entries = 64U;

class ProtectedObjectIdSource {
public:
    virtual ~ProtectedObjectIdSource() = default;
    [[nodiscard]] virtual os::core::Result<ProtectedObjectId> next() noexcept = 0;
};

struct ProtectedNamespaceEntry final {
    os::core::PrincipalId principal {};
    os::core::UserId user {};
    RelativePath path {};
    ProtectedObjectId object_id {};
    std::uint64_t generation {0U};

    [[nodiscard]] bool valid() const noexcept {
        return os::core::valid_principal(principal) && user.value() != 0U && path.valid() &&
            object_id.valid() && generation != 0U;
    }

    [[nodiscard]] ProtectedObjectVersion version() const noexcept {
        return ProtectedObjectVersion{.user = user, .object_id = object_id, .generation = generation};
    }
};

namespace protected_namespace_errors {
inline constexpr std::uint32_t not_found = 1U;
inline constexpr std::uint32_t already_exists = 2U;
inline constexpr std::uint32_t capacity = 3U;
inline constexpr std::uint32_t generation_conflict = 4U;
inline constexpr std::uint32_t invalid_identity = 5U;
inline constexpr std::uint32_t duplicate_object = 6U;
} // namespace protected_namespace_errors

[[nodiscard]] constexpr os::core::Error protected_namespace_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::storage, 0x740U + code);
}

// Trusted namespace metadata. A pathname selects an entry, but the stable random
// object ID is the cryptographic identity. Replacement first *proposes* a newer
// version without mutation. The registry changes only when publication is ready
// to make that version authoritative.
class ProtectedNamespaceRegistry final {
public:
    explicit ProtectedNamespaceRegistry(ProtectedObjectIdSource& ids) noexcept : ids_(&ids) {}

    [[nodiscard]] os::core::Result<ProtectedNamespaceEntry>
    create(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const RelativePath& path) noexcept;

    [[nodiscard]] const ProtectedNamespaceEntry*
    find(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const RelativePath& path) const noexcept;

    [[nodiscard]] ProtectedNamespaceEntry*
    find(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const RelativePath& path) noexcept;

    [[nodiscard]] os::core::Result<ProtectedObjectVersion>
    propose_next_generation(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const RelativePath& path,
        std::uint64_t expected_generation) const noexcept;

    [[nodiscard]] os::core::Result<void>
    publish_generation(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const RelativePath& path,
        std::uint64_t expected_generation,
        std::uint64_t new_generation) noexcept;

    // Snapshot support is intentionally bounded and all-or-nothing. Restore
    // validates the entire replacement set, including duplicate namespace keys
    // and duplicate stable object IDs, before mutating trusted registry state.
    [[nodiscard]] os::core::Result<std::size_t>
    copy_user_entries(
        os::core::UserId user,
        std::span<ProtectedNamespaceEntry> output) const noexcept;

    [[nodiscard]] os::core::Result<void>
    replace_user_entries(
        os::core::UserId user,
        std::span<const ProtectedNamespaceEntry> entries) noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Slot final {
        bool occupied {false};
        ProtectedNamespaceEntry entry {};
    };

    ProtectedObjectIdSource* ids_ {nullptr};
    std::array<Slot, max_protected_namespace_entries> slots_ {};
};

} // namespace os::storage
