#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/package/package.hpp>

namespace os::app {

inline constexpr std::size_t m1_application_principal_capacity = 64U;
inline constexpr std::size_t max_application_principal_snapshot_bytes = 16U * 1024U;
inline constexpr std::uint32_t application_principal_snapshot_magic_v1 = 0x31495045U; // "EPI1" LE
inline constexpr std::uint16_t application_principal_snapshot_version_v1 = 1U;
inline constexpr std::uint16_t application_principal_snapshot_header_size_v1 = 32U;
inline constexpr std::uint64_t application_principal_high_v1 = 0x4150502E454D4E4CULL; // "APP.EMNL"

namespace principal_errors {
inline constexpr std::uint32_t invalid_state_directory = 200U;
inline constexpr std::uint32_t io_failure = 201U;
inline constexpr std::uint32_t snapshot_too_large = 202U;
inline constexpr std::uint32_t malformed_snapshot = 203U;
inline constexpr std::uint32_t unsupported_snapshot_version = 204U;
inline constexpr std::uint32_t snapshot_inconsistent = 205U;
inline constexpr std::uint32_t store_full = 206U;
inline constexpr std::uint32_t id_exhausted = 207U;
inline constexpr std::uint32_t invalid_application = 208U;
} // namespace principal_errors

struct ApplicationPrincipalRecord final {
    os::package::ApplicationIdentity application {};
    os::core::UserId user {};
    os::core::PrincipalId principal {};

    [[nodiscard]] bool valid() const noexcept {
        return application.valid() && os::core::valid_principal(principal) &&
            principal.high == application_principal_high_v1 && principal.low != 0U;
    }

    [[nodiscard]] friend bool
    operator==(const ApplicationPrincipalRecord& lhs, const ApplicationPrincipalRecord& rhs) noexcept {
        return lhs.application == rhs.application && lhs.user == rhs.user &&
            lhs.principal == rhs.principal;
    }
};

// Device-local persistent allocator for application principals. Principal IDs
// are identifiers, not secrets and not cryptographic proofs. Uniqueness comes
// from trusted monotonic allocation and durable non-reuse, not randomness.
class ApplicationPrincipalStore final {
public:
    ApplicationPrincipalStore(const ApplicationPrincipalStore&) = delete;
    ApplicationPrincipalStore& operator=(const ApplicationPrincipalStore&) = delete;
    ApplicationPrincipalStore(ApplicationPrincipalStore&&) noexcept = default;
    ApplicationPrincipalStore& operator=(ApplicationPrincipalStore&&) noexcept = default;
    ~ApplicationPrincipalStore() = default;

    [[nodiscard]] static os::core::Result<ApplicationPrincipalStore>
    open(os::core::NativeHandle state_directory) noexcept;

    [[nodiscard]] os::core::Result<ApplicationPrincipalRecord>
    resolve_or_allocate(
        const os::package::ApplicationIdentity& application,
        os::core::UserId user) noexcept;

    [[nodiscard]] os::core::Result<ApplicationPrincipalRecord>
    lookup(
        const os::package::ApplicationIdentity& application,
        os::core::UserId user) const noexcept;

    [[nodiscard]] std::size_t record_count() const noexcept { return record_count_; }
    [[nodiscard]] std::uint64_t next_principal_low() const noexcept { return next_principal_low_; }

private:
    ApplicationPrincipalStore(
        os::core::NativeHandle state_directory,
        std::array<ApplicationPrincipalRecord, m1_application_principal_capacity> records,
        std::size_t record_count,
        std::uint64_t next_principal_low) noexcept
        : state_directory_(static_cast<os::core::NativeHandle&&>(state_directory)),
          records_(records),
          record_count_(record_count),
          next_principal_low_(next_principal_low) {}

    [[nodiscard]] static os::core::Result<ApplicationPrincipalStore>
    load(os::core::NativeHandle state_directory) noexcept;

    [[nodiscard]] os::core::Result<void>
    persist_candidate(
        const std::array<ApplicationPrincipalRecord, m1_application_principal_capacity>& records,
        std::size_t record_count,
        std::uint64_t next_principal_low) noexcept;

    os::core::NativeHandle state_directory_ {};
    std::array<ApplicationPrincipalRecord, m1_application_principal_capacity> records_ {};
    std::size_t record_count_ {0U};
    std::uint64_t next_principal_low_ {1U};
};

} // namespace os::app
