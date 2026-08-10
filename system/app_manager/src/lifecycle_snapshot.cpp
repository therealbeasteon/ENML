#include <os/app/manager.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/core/error.hpp>

namespace os::app {
namespace {

[[nodiscard]] constexpr os::core::Error manager_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

void sort_by_instance(ApplicationLifecycleSnapshot& snapshot) noexcept {
    for (std::size_t index = 1U; index < snapshot.count; ++index) {
        ApplicationLifecycleRecord value = snapshot.applications[index];
        std::size_t cursor = index;
        while (cursor > 0U &&
               snapshot.applications[cursor - 1U].instance.value() > value.instance.value()) {
            snapshot.applications[cursor] = snapshot.applications[cursor - 1U];
            --cursor;
        }
        snapshot.applications[cursor] = value;
    }
}

[[nodiscard]] bool same_live_set(
    const ApplicationLifecycleSnapshot& left,
    const ApplicationLifecycleSnapshot& right) noexcept {
    if (left.count != right.count) return false;
    for (std::size_t index = 0U; index < left.count; ++index) {
        if (left.applications[index] != right.applications[index]) return false;
    }
    return true;
}

} // namespace

os::core::Result<ApplicationLifecycleSnapshot>
ApplicationManager::lifecycle_snapshot() const noexcept {
    ApplicationLifecycleSnapshot current{};
    for (const auto& slot : instances_) {
        if (!slot.occupied) continue;
        if (current.count >= current.applications.size() || !slot.info.valid()) {
            return manager_error(manager_errors::invalid_target);
        }
        current.applications[current.count++] = ApplicationLifecycleRecord{
            .instance = slot.info.instance,
            .application = slot.info.application,
            .identity = slot.info.identity,
        };
    }
    sort_by_instance(current);

    if (!lifecycle_cache_initialized_) {
        if (lifecycle_revision_ == 0U) {
            return manager_error(manager_errors::lifecycle_revision_exhausted);
        }
        current.revision = lifecycle_revision_;
        lifecycle_cache_ = current;
        lifecycle_cache_initialized_ = true;
        return lifecycle_cache_;
    }

    if (same_live_set(current, lifecycle_cache_)) return lifecycle_cache_;

    if (lifecycle_revision_ == 0U ||
        lifecycle_revision_ == std::numeric_limits<std::uint64_t>::max()) {
        lifecycle_revision_ = 0U;
        lifecycle_cache_ = {};
        lifecycle_cache_initialized_ = false;
        return manager_error(manager_errors::lifecycle_revision_exhausted);
    }

    ++lifecycle_revision_;
    current.revision = lifecycle_revision_;
    lifecycle_cache_ = current;
    return lifecycle_cache_;
}

} // namespace os::app
