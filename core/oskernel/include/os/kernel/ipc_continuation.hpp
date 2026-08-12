#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel {

namespace ipc_continuation_errors {
inline constexpr std::uint32_t invalid_thread = 250U;
inline constexpr std::uint32_t invalid_epoch = 251U;
inline constexpr std::uint32_t invalid_exchange = 252U;
inline constexpr std::uint32_t already_armed = 253U;
inline constexpr std::uint32_t exhausted = 254U;
inline constexpr std::uint32_t not_armed = 255U;
inline constexpr std::uint32_t stale = 256U;
}

struct IpcSendContinuation final {
    ThreadId caller {invalid_thread};
    AddressSpaceEpoch epoch {};
    std::uint64_t exchange_address {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return caller != invalid_thread && epoch.valid() && exchange_address != 0ULL;
    }
};

class IpcContinuationTable final {
public:
    [[nodiscard]] os::core::Result<void> arm(
        ThreadId caller,
        AddressSpaceEpoch epoch,
        std::uint64_t exchange_address,
        const AddressSpaceEpochAuthority& epochs) noexcept;

    [[nodiscard]] os::core::Result<IpcSendContinuation> take(
        ThreadId caller,
        AddressSpaceEpoch expected,
        const AddressSpaceEpochAuthority& epochs) noexcept;

    [[nodiscard]] os::core::Result<void> cancel(ThreadId caller) noexcept;
    void release_thread(ThreadId caller) noexcept;

    [[nodiscard]] std::size_t count() const noexcept { return occupied_; }

private:
    struct Slot final {
        bool occupied {false};
        IpcSendContinuation continuation {};
    };

    [[nodiscard]] Slot* slot_for(ThreadId caller) noexcept;

    std::array<Slot, max_threads> slots_ {};
    std::size_t occupied_ {0U};
};

} // namespace os::kernel
