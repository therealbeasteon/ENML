#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/process_translation.hpp>

namespace os::kernel {

inline constexpr std::uint64_t user_access_page_size = 4096ULL;

enum class UserAccessIntent : std::uint8_t {
    read_from_user = 1U,
    write_to_user = 2U,
};

struct UserRange final {
    std::uint64_t address {0ULL};
    std::size_t length {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        if (address == 0ULL || length == 0U) return false;
        if (length > user_access_page_size) return false;
        const auto bytes = static_cast<std::uint64_t>(length);
        if (address > UINT64_MAX - bytes) return false;
        const std::uint64_t last = address + bytes - 1ULL;
        return (address / user_access_page_size) == (last / user_access_page_size);
    }
};

// A ticket is a prepared privileged access, not a pointer blessing. It binds
// one exact process-memory incarnation, one direction and one page-contained
// range. The machine copy routine must revalidate the epoch immediately before
// opening its PAN/user-access window.
struct UserAccessTicket final {
    ThreadId thread {invalid_thread};
    AddressSpaceEpoch epoch {};
    std::uint64_t root_physical {0ULL};
    UserRange range {};
    UserAccessIntent intent {UserAccessIntent::read_from_user};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return thread != invalid_thread && epoch.valid() && root_physical != 0ULL && range.valid();
    }
};

namespace user_access_errors {
inline constexpr std::uint32_t invalid_range = 200U;
inline constexpr std::uint32_t stale_translation = 201U;
inline constexpr std::uint32_t epoch_mismatch = 202U;
} // namespace user_access_errors

[[nodiscard]] os::core::Result<UserAccessTicket> prepare_user_access(
    ThreadId thread,
    AddressSpaceEpoch expected_epoch,
    UserRange range,
    UserAccessIntent intent,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

[[nodiscard]] bool user_access_still_live(
    const UserAccessTicket& ticket,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

} // namespace os::kernel
