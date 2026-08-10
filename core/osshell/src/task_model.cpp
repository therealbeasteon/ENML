#include <os/shell/task_model.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/shell/error.hpp>

namespace os::shell {
namespace {

[[nodiscard]] bool exact_task_identity(
    const ShellTask& left,
    const ShellTask& right) noexcept {
    return left.instance == right.instance &&
        left.application == right.application &&
        left.owner == right.owner;
}

[[nodiscard]] bool exact_task(const ShellTask& left, const ShellTask& right) noexcept {
    return exact_task_identity(left, right) &&
        left.root_surface == right.root_surface &&
        left.activation_serial == right.activation_serial;
}

[[nodiscard]] bool scene_role_valid(os::display::SurfaceRole role) noexcept {
    switch (role) {
    case os::display::SurfaceRole::application:
    case os::display::SurfaceRole::popup:
    case os::display::SurfaceRole::system_chrome:
    case os::display::SurfaceRole::secure_system:
        return true;
    }
    return false;
}

[[nodiscard]] bool trust_matches_role(const os::display::SceneEntry& entry) noexcept {
    switch (entry.surface.role) {
    case os::display::SurfaceRole::application:
    case os::display::SurfaceRole::popup:
        return entry.trusted_presentation == os::display::TrustedPresentation::none;
    case os::display::SurfaceRole::system_chrome:
        return entry.trusted_presentation == os::display::TrustedPresentation::system_chrome;
    case os::display::SurfaceRole::secure_system:
        return entry.trusted_presentation == os::display::TrustedPresentation::secure_system;
    }
    return false;
}

[[nodiscard]] bool scene_entry_valid(const os::display::SceneEntry& entry) noexcept {
    if (!entry.surface.valid() || !scene_role_valid(entry.surface.role) ||
        !trust_matches_role(entry)) {
        return false;
    }
    if (entry.surface.role == os::display::SurfaceRole::popup) {
        return os::display::valid_display_object_value(entry.surface.parent.value());
    }
    return entry.surface.parent.value() == 0U;
}

[[nodiscard]] const ShellTask* find_task(
    const std::array<ShellTask, max_shell_tasks>& tasks,
    std::size_t count,
    os::core::ApplicationInstanceId instance) noexcept {
    const std::size_t limit = std::min(count, tasks.size());
    for (std::size_t index = 0U; index < limit; ++index) {
        if (tasks[index].instance == instance) return &tasks[index];
    }
    return nullptr;
}

[[nodiscard]] bool desired_contains(
    const std::array<ShellTask, max_shell_tasks>& tasks,
    std::size_t count,
    os::core::ApplicationInstanceId instance) noexcept {
    return find_task(tasks, count, instance) != nullptr;
}

void sort_by_instance(
    std::array<ShellTask, max_shell_tasks>& tasks,
    std::size_t count) noexcept {
    // Fixed-capacity insertion sort: at most 16 records, no allocator and no
    // dependency on publisher slot order. Stable task ordering prevents an
    // App Manager table layout from becoming shell-visible task order.
    for (std::size_t index = 1U; index < count; ++index) {
        ShellTask value = tasks[index];
        std::size_t cursor = index;
        while (cursor > 0U &&
               tasks[cursor - 1U].instance.value() > value.instance.value()) {
            tasks[cursor] = tasks[cursor - 1U];
            --cursor;
        }
        tasks[cursor] = value;
    }
}

} // namespace

ShellTask* ShellTaskModel::find(os::core::ApplicationInstanceId instance) noexcept {
    for (std::size_t index = 0U; index < task_count_; ++index) {
        if (tasks_[index].instance == instance) return &tasks_[index];
    }
    return nullptr;
}

const ShellTask* ShellTaskModel::find(
    os::core::ApplicationInstanceId instance) const noexcept {
    for (std::size_t index = 0U; index < task_count_; ++index) {
        if (tasks_[index].instance == instance) return &tasks_[index];
    }
    return nullptr;
}

os::core::Result<void> ShellTaskModel::advance_revision() noexcept {
    if (revision_.value() == std::numeric_limits<std::uint64_t>::max()) {
        return shell_error(errors::revision_exhausted);
    }
    revision_ = ShellRevision{revision_.value() + 1U};
    return {};
}

os::core::Result<void> ShellTaskModel::publish(ShellTask task) noexcept {
    if (!task.valid() || task.activation_serial != 0U) {
        return shell_error(errors::invalid_task);
    }

    if (ShellTask* existing = find(task.instance); existing != nullptr) {
        if (!exact_task_identity(*existing, task)) {
            return shell_error(errors::task_conflict);
        }
        for (std::size_t index = 0U; index < task_count_; ++index) {
            const ShellTask& other = tasks_[index];
            if (other.instance != task.instance && other.root_surface == task.root_surface) {
                return shell_error(errors::task_conflict);
            }
        }
        if (existing->root_surface == task.root_surface) return {};
        if (task.root_surface.value() < existing->root_surface.value()) {
            return shell_error(errors::stale_scene_snapshot);
        }
        auto revision = advance_revision();
        if (!revision) return revision.error();
        existing->root_surface = task.root_surface;
        return {};
    }

    if (task_count_ >= tasks_.size()) return shell_error(errors::task_capacity);
    for (std::size_t index = 0U; index < task_count_; ++index) {
        if (tasks_[index].owner == task.owner || tasks_[index].root_surface == task.root_surface) {
            return shell_error(errors::task_conflict);
        }
    }

    auto revision = advance_revision();
    if (!revision) return revision.error();
    tasks_[task_count_++] = task;
    return {};
}

os::core::Result<void> ShellTaskModel::reconcile(
    const os::app::ApplicationLifecycleSnapshot& applications,
    const os::display::SceneSnapshot& scene) noexcept {
    if (applications.revision == 0U || applications.count > applications.applications.size()) {
        return shell_error(errors::invalid_lifecycle_snapshot);
    }
    if (applications.revision < last_lifecycle_revision_) {
        return shell_error(errors::stale_lifecycle_snapshot);
    }
    if (scene.count > scene.entries.size()) {
        return shell_error(errors::invalid_scene_snapshot);
    }

    for (std::size_t index = 0U; index < applications.count; ++index) {
        const os::app::ApplicationLifecycleRecord& record = applications.applications[index];
        if (!record.valid()) return shell_error(errors::invalid_lifecycle_snapshot);
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            const os::app::ApplicationLifecycleRecord& previous = applications.applications[earlier];
            if (previous.instance == record.instance || previous.identity == record.identity) {
                return shell_error(errors::invalid_lifecycle_snapshot);
            }
        }
    }

    for (std::size_t index = 0U; index < scene.count; ++index) {
        const os::display::SceneEntry& entry = scene.entries[index];
        if (!scene_entry_valid(entry)) return shell_error(errors::invalid_scene_snapshot);
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (scene.entries[earlier].surface.id == entry.surface.id) {
                return shell_error(errors::invalid_scene_snapshot);
            }
        }
    }

    std::array<ShellTask, max_shell_tasks> desired{};
    std::size_t desired_count = 0U;
    for (std::size_t application_index = 0U;
         application_index < applications.count;
         ++application_index) {
        const os::app::ApplicationLifecycleRecord& record =
            applications.applications[application_index];
        const os::display::SceneEntry* root = nullptr;
        for (std::size_t scene_index = 0U; scene_index < scene.count; ++scene_index) {
            const os::display::SceneEntry& entry = scene.entries[scene_index];
            if (entry.surface.role != os::display::SurfaceRole::application ||
                entry.surface.owner != record.identity) {
                continue;
            }
            if (root != nullptr) return shell_error(errors::invalid_scene_snapshot);
            root = &entry;
        }

        // Neither trusted source can mint a complete shell task by itself.
        // Lifecycle without a root waits; an orphan compositor root is ignored.
        if (root == nullptr) continue;
        if (desired_count >= desired.size()) return shell_error(errors::task_capacity);

        ShellTask joined{
            .instance = record.instance,
            .application = record.application,
            .owner = record.identity,
            .root_surface = root->surface.id,
        };
        if (const ShellTask* existing = find(record.instance); existing != nullptr) {
            if (!exact_task_identity(*existing, joined)) {
                return shell_error(errors::task_conflict);
            }
            if (joined.root_surface.value() < existing->root_surface.value()) {
                return shell_error(errors::stale_scene_snapshot);
            }
            joined.activation_serial = existing->activation_serial;
        }

        for (std::size_t earlier = 0U; earlier < desired_count; ++earlier) {
            if (desired[earlier].root_surface == joined.root_surface ||
                desired[earlier].owner == joined.owner) {
                return shell_error(errors::invalid_scene_snapshot);
            }
        }
        desired[desired_count++] = joined;
    }

    sort_by_instance(desired, desired_count);

    bool changed = desired_count != task_count_;
    if (!changed) {
        for (std::size_t index = 0U; index < desired_count; ++index) {
            const ShellTask* existing = find_task(tasks_, task_count_, desired[index].instance);
            if (existing == nullptr || !exact_task(*existing, desired[index])) {
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        last_lifecycle_revision_ = applications.revision;
        return {};
    }

    auto revision = advance_revision();
    if (!revision) return revision.error();

    tasks_ = desired;
    task_count_ = desired_count;
    last_lifecycle_revision_ = applications.revision;
    if (active_instance_.value() != 0U &&
        !desired_contains(tasks_, task_count_, active_instance_)) {
        active_instance_ = {};
        view_ = ShellView::home;
    }
    return {};
}

os::core::Result<void> ShellTaskModel::remove(
    os::core::ApplicationInstanceId instance) noexcept {
    if (instance.value() == 0U) return shell_error(errors::invalid_task);

    std::size_t index = task_count_;
    for (std::size_t candidate = 0U; candidate < task_count_; ++candidate) {
        if (tasks_[candidate].instance == instance) {
            index = candidate;
            break;
        }
    }
    if (index == task_count_) return shell_error(errors::unknown_task);

    auto revision = advance_revision();
    if (!revision) return revision.error();

    for (std::size_t next = index + 1U; next < task_count_; ++next) {
        tasks_[next - 1U] = tasks_[next];
    }
    --task_count_;
    tasks_[task_count_] = ShellTask{};

    if (active_instance_ == instance) {
        active_instance_ = {};
        view_ = ShellView::home;
    }
    return {};
}

os::core::Result<void> ShellTaskModel::activate(
    os::core::ApplicationInstanceId instance) noexcept {
    if (instance.value() == 0U) return shell_error(errors::invalid_task);
    ShellTask* task = find(instance);
    if (task == nullptr) return shell_error(errors::unknown_task);
    if (next_activation_serial_ == 0U) {
        return shell_error(errors::activation_serial_exhausted);
    }

    auto revision = advance_revision();
    if (!revision) return revision.error();

    task->activation_serial = next_activation_serial_;
    if (next_activation_serial_ == std::numeric_limits<std::uint64_t>::max()) {
        next_activation_serial_ = 0U;
    } else {
        ++next_activation_serial_;
    }
    active_instance_ = instance;
    view_ = ShellView::application;
    return {};
}

os::core::Result<void> ShellTaskModel::show_home() noexcept {
    if (view_ == ShellView::home && active_instance_.value() == 0U) return {};
    auto revision = advance_revision();
    if (!revision) return revision.error();
    view_ = ShellView::home;
    active_instance_ = {};
    return {};
}

os::core::Result<void> ShellTaskModel::show_overview() noexcept {
    if (view_ == ShellView::overview) return {};
    auto revision = advance_revision();
    if (!revision) return revision.error();
    view_ = ShellView::overview;
    return {};
}

ShellSnapshot ShellTaskModel::snapshot() const noexcept {
    ShellSnapshot output{
        .revision = revision_,
        .view = view_,
        .active_instance = active_instance_,
        .task_count = task_count_,
    };
    for (std::size_t index = 0U; index < task_count_; ++index) {
        output.tasks[index] = tasks_[index];
    }
    return output;
}

} // namespace os::shell
