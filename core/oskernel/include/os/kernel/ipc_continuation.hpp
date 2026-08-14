#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/capability.hpp>
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
inline constexpr std::uint32_t invalid_capability = 257U;
}

struct IpcSendContinuation final {
    ThreadId caller {invalid_thread};
    AddressSpaceEpoch epoch {};
    std::uint64_t exchange_address {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return caller != invalid_thread && epoch.valid() && exchange_address != 0ULL;
    }
};

struct IpcReceiveContinuation final {
    ThreadId server {invalid_thread};
    AddressSpaceEpoch epoch {};
    CapabilityId endpoint_capability {invalid_capability};
    std::uint64_t exchange_address {0ULL};
    // Absolute, on the kernel's monotonic clock; zero means no deadline. The
    // ABI takes a *relative* value (docs/M7_2_SERVER_LOOP.md) so the caller
    // needs no clock; the conversion happens once, here, at arm time. Storing
    // the absolute form is what makes it immune to being re-armed later and
    // silently extended - a relative value re-based on each wakeup drifts, and
    // drift in a bound is indistinguishable from not having one.
    std::uint64_t deadline_nanoseconds {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return server != invalid_thread && epoch.valid() &&
            endpoint_capability != invalid_capability && exchange_address != 0ULL;
    }
    [[nodiscard]] constexpr bool bounded() const noexcept {
        return deadline_nanoseconds != 0ULL;
    }
    [[nodiscard]] constexpr bool expired_at(std::uint64_t now) const noexcept {
        return bounded() && now >= deadline_nanoseconds;
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

    // `deadline_nanoseconds` is absolute and zero means unbounded. Callers pass
    // the already-converted value so this table needs no clock of its own and
    // stays a pure state machine, which is what keeps it host-testable.
    [[nodiscard]] os::core::Result<void> arm_receive(
        ThreadId server,
        AddressSpaceEpoch epoch,
        CapabilityId endpoint_capability,
        std::uint64_t exchange_address,
        const AddressSpaceEpochAuthority& epochs,
        std::uint64_t deadline_nanoseconds = 0ULL) noexcept;

    // The soonest deadline among armed receives, or zero if none is bounded.
    // This is what a tickless scheduler arms its timer against: one wakeup for
    // the whole table rather than one per waiter.
    [[nodiscard]] std::uint64_t earliest_receive_deadline() const noexcept;

    // Takes the continuation of one receiver whose deadline has passed, so the
    // wake path can complete its receive with no message. Ties break on the
    // lowest ThreadId, deterministically: which of two equally-expired waiters
    // runs first must not depend on table layout, or the order becomes a
    // channel between them.
    //
    // Returns not_armed when nothing has expired. It deliberately reports only
    // the expired thread's own continuation and never how many others were
    // waiting - expiry tells a caller its bound passed, and nothing about the
    // rest of the table.
    [[nodiscard]] os::core::Result<IpcReceiveContinuation> take_expired_receive(
        std::uint64_t now_nanoseconds) noexcept;

    [[nodiscard]] os::core::Result<IpcReceiveContinuation> take_receive(
        ThreadId server,
        AddressSpaceEpoch expected,
        const AddressSpaceEpochAuthority& epochs) noexcept;

    [[nodiscard]] os::core::Result<void> cancel_receive(ThreadId server) noexcept;

    void release_thread(ThreadId thread) noexcept;

    [[nodiscard]] bool send_armed(ThreadId caller) const noexcept;
    [[nodiscard]] bool receive_armed(ThreadId server) const noexcept;
    [[nodiscard]] std::size_t count() const noexcept { return occupied_; }

private:
    struct SendSlot final {
        bool occupied {false};
        IpcSendContinuation continuation {};
    };
    struct ReceiveSlot final {
        bool occupied {false};
        IpcReceiveContinuation continuation {};
    };

    [[nodiscard]] SendSlot* send_slot_for(ThreadId caller) noexcept;
    [[nodiscard]] ReceiveSlot* receive_slot_for(ThreadId server) noexcept;

    std::array<SendSlot, max_threads> send_slots_ {};
    std::array<ReceiveSlot, max_threads> receive_slots_ {};
    std::size_t occupied_ {0U};
};

} // namespace os::kernel
