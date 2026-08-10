#include <os/shell/task_model.hpp>

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
        auto revision = advance_revision();
        if (!revision) return revision.error();
        existing->root_surface = task.root_surface;
        return {};
    }

    if (task_count_ >= tasks_.size()) return shell_error(errors::task_capacity);
    for (std::size_t index = 0U; index < task_count_; ++index) {
        // One live exact process/root surface maps to one shell task. Multiple
        // instances of the same signed application remain allowed when App
        // Manager deliberately launches them as distinct PeerIdentity values.
        if (tasks_[index].owner == task.owner || tasks_[index].root_surface == task.root_surface) {
            return shell_error(errors::task_conflict);
        }
    }

    auto revision = advance_revision();
    if (!revision) return revision.error();
    tasks_[task_count_++] = task;
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
    // Preserve active_instance_ as the last foreground application so overview
    // can return to it without guessing from task order. The compositor commit
    // layer decides which surfaces are actually visible while overview is up.
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
