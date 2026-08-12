#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/rendezvous.hpp>
#include <os/kernel/translation_root.hpp>

namespace os::kernel {

namespace process_translation_errors {
inline constexpr std::uint32_t invalid_thread = 1U;
inline constexpr std::uint32_t invalid_epoch = 2U;
inline constexpr std::uint32_t invalid_root = 3U;
inline constexpr std::uint32_t duplicate_thread = 4U;
inline constexpr std::uint32_t duplicate_epoch = 5U;
inline constexpr std::uint32_t exhausted = 6U;
inline constexpr std::uint32_t not_found = 7U;
inline constexpr std::uint32_t stale = 8U;
} // namespace process_translation_errors

struct ProcessTranslationBinding final {
    ThreadId thread {invalid_thread};
    AddressSpaceEpoch epoch {};
    std::uint64_t root_physical {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return thread != invalid_thread && epoch.valid() && root_physical != 0ULL;
    }
};

// The scheduler chooses threads; this table is the sole bridge from a runnable
// thread to a process translation identity. IRQ code never owns ASIDs or roots.
// A stale epoch therefore cannot become executable merely because a thread id
// remains present in scheduler state.
class ProcessTranslationTable final {
public:
    // Binding is itself an authority transition. Cookie requires both a live
    // epoch and a minted sealed-root capability; a raw page-table address is not
    // sufficient execution authority.
    [[nodiscard]] os::core::Result<void> bind(
        ThreadId thread,
        AddressSpaceEpoch epoch,
        SealedTranslationRoot root,
        const AddressSpaceEpochAuthority& epochs) noexcept;

    [[nodiscard]] os::core::Result<ProcessTranslationBinding> resolve(
        ThreadId thread,
        const AddressSpaceEpochAuthority& epochs) const noexcept;

    [[nodiscard]] os::core::Result<void> retire(
        ThreadId thread,
        AddressSpaceEpoch epoch) noexcept;

    [[nodiscard]] std::size_t count() const noexcept { return occupied_; }

private:
    struct Slot final {
        bool occupied {false};
        ProcessTranslationBinding binding {};
    };

    std::array<Slot, max_threads> slots_ {};
    std::size_t occupied_ {0U};
};

} // namespace os::kernel
