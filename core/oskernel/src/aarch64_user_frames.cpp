#include <os/kernel/aarch64_user_frames.hpp>

#include <os/core/error.hpp>

namespace os::kernel::aarch64 {
namespace {
[[nodiscard]] constexpr os::core::Error frame_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

UserFrameTable::Slot* UserFrameTable::find(ThreadId thread) noexcept {
    for (auto& slot : slots_) if (slot.occupied && slot.thread == thread) return &slot;
    return nullptr;
}

const UserFrameTable::Slot* UserFrameTable::find(ThreadId thread) const noexcept {
    for (const auto& slot : slots_) if (slot.occupied && slot.thread == thread) return &slot;
    return nullptr;
}

bool UserFrameTable::valid_el0_frame(const ExceptionFrame& frame) noexcept {
    // SPSR.M[3:0] == 0000 means EL0t. Reject frames that would resume into EL1
    // or another execution state regardless of who supplied the ThreadId.
    return (frame.spsr_el1 & 0xFULL) == 0ULL && frame.elr_el1 != 0ULL && frame.sp_el0 != 0ULL;
}

os::core::Result<void> UserFrameTable::admit(
    ThreadId thread, const ExceptionFrame& initial) noexcept {
    if (thread == invalid_thread) return frame_error(user_frame_errors::invalid_thread);
    if (!valid_el0_frame(initial)) return frame_error(user_frame_errors::invalid_el0_frame);
    if (find(thread) != nullptr) return frame_error(user_frame_errors::duplicate_thread);
    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{thread, initial, true};
        ++occupied_;
        return {};
    }
    return frame_error(user_frame_errors::exhausted);
}

os::core::Result<void> UserFrameTable::capture(
    ThreadId thread, const ExceptionFrame& live) noexcept {
    if (!valid_el0_frame(live)) return frame_error(user_frame_errors::invalid_el0_frame);
    auto* slot = find(thread);
    if (slot == nullptr) return frame_error(user_frame_errors::unknown_thread);
    slot->frame = live;
    return {};
}

os::core::Result<void> UserFrameTable::restore(
    ThreadId thread, ExceptionFrame& live) const noexcept {
    const auto* slot = find(thread);
    if (slot == nullptr) return frame_error(user_frame_errors::unknown_thread);
    if (!valid_el0_frame(slot->frame)) return frame_error(user_frame_errors::invalid_el0_frame);
    live = slot->frame;
    return {};
}

os::core::Result<void> UserFrameTable::retire(ThreadId thread) noexcept {
    auto* slot = find(thread);
    if (slot == nullptr) return frame_error(user_frame_errors::unknown_thread);
    *slot = Slot{};
    --occupied_;
    return {};
}

bool UserFrameTable::contains(ThreadId thread) const noexcept {
    return find(thread) != nullptr;
}

} // namespace os::kernel::aarch64
