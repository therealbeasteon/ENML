#include <os/kernel/process_translation.hpp>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_translation.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

os::core::Result<void> ProcessTranslationTable::bind(
    ThreadId thread,
    AddressSpaceEpoch epoch,
    std::uint64_t root_physical) noexcept {
    if (thread == invalid_thread) return error(process_translation_errors::invalid_thread);
    if (!epoch.valid()) return error(process_translation_errors::invalid_epoch);
    if (!aarch64::stage1_physical_address(root_physical)) {
        return error(process_translation_errors::invalid_root);
    }

    for (const auto& slot : slots_) {
        if (!slot.occupied) continue;
        if (slot.binding.thread == thread) {
            return error(process_translation_errors::duplicate_thread);
        }
        if (slot.binding.epoch == epoch) {
            return error(process_translation_errors::duplicate_epoch);
        }
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot.occupied = true;
        slot.binding = ProcessTranslationBinding{thread, epoch, root_physical};
        ++occupied_;
        return {};
    }
    return error(process_translation_errors::exhausted);
}

os::core::Result<ProcessTranslationBinding> ProcessTranslationTable::resolve(
    ThreadId thread,
    const AddressSpaceEpochAuthority& epochs) const noexcept {
    if (thread == invalid_thread) {
        return error(process_translation_errors::invalid_thread);
    }
    for (const auto& slot : slots_) {
        if (!slot.occupied || slot.binding.thread != thread) continue;
        if (!epochs.active(slot.binding.epoch)) {
            return error(process_translation_errors::stale);
        }
        return slot.binding;
    }
    return error(process_translation_errors::not_found);
}

os::core::Result<void> ProcessTranslationTable::retire(
    ThreadId thread,
    AddressSpaceEpoch epoch) noexcept {
    if (thread == invalid_thread) return error(process_translation_errors::invalid_thread);
    if (!epoch.valid()) return error(process_translation_errors::invalid_epoch);
    for (auto& slot : slots_) {
        if (!slot.occupied || slot.binding.thread != thread) continue;
        if (!(slot.binding.epoch == epoch)) {
            return error(process_translation_errors::stale);
        }
        slot = Slot{};
        --occupied_;
        return {};
    }
    return error(process_translation_errors::not_found);
}

} // namespace os::kernel
