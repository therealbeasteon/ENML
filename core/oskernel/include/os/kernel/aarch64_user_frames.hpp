#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/aarch64_exception.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel::aarch64 {

namespace user_frame_errors {
inline constexpr std::uint32_t invalid_thread = 110U;
inline constexpr std::uint32_t duplicate_thread = 111U;
inline constexpr std::uint32_t unknown_thread = 112U;
inline constexpr std::uint32_t exhausted = 113U;
inline constexpr std::uint32_t invalid_el0_frame = 114U;
} // namespace user_frame_errors

// Scheduler-owned architectural state for resumable EL0 threads. The live IRQ
// frame on the guarded EL1 stack is transient; this table is the durable state
// that survives preemption. It is fixed-size and allocation-free.
class UserFrameTable final {
public:
    [[nodiscard]] os::core::Result<void> admit(
        ThreadId thread,
        const ExceptionFrame& initial) noexcept;

    // Non-mutating switch preflight. Cookie validates every failure-prone frame
    // condition before committing a scheduler/MMU transition, so an invalid live
    // frame or corrupt saved frame cannot leave half of a context switch applied.
    [[nodiscard]] os::core::Result<void> validate_capture(
        ThreadId thread,
        const ExceptionFrame& live) const noexcept;
    [[nodiscard]] os::core::Result<void> validate_restore(ThreadId thread) const noexcept;

    [[nodiscard]] os::core::Result<void> capture(
        ThreadId thread,
        const ExceptionFrame& live) noexcept;
    [[nodiscard]] os::core::Result<void> restore(
        ThreadId thread,
        ExceptionFrame& live) const noexcept;
    [[nodiscard]] os::core::Result<void> retire(ThreadId thread) noexcept;
    [[nodiscard]] bool contains(ThreadId thread) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return occupied_; }

private:
    struct Slot final {
        ThreadId thread {invalid_thread};
        ExceptionFrame frame {};
        bool occupied {false};
    };

    [[nodiscard]] Slot* find(ThreadId thread) noexcept;
    [[nodiscard]] const Slot* find(ThreadId thread) const noexcept;
    [[nodiscard]] static bool valid_el0_frame(const ExceptionFrame& frame) noexcept;

    std::array<Slot, max_threads> slots_ {};
    std::size_t occupied_ {0U};
};

} // namespace os::kernel::aarch64
