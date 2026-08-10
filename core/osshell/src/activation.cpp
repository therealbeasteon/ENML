#include <os/shell/activation.hpp>

#include <cstddef>

#include <os/shell/error.hpp>

namespace os::shell {
namespace {

[[nodiscard]] const ShellTask* find_task(
    const ShellSnapshot& snapshot,
    os::core::ApplicationInstanceId instance) noexcept {
    if (snapshot.task_count > snapshot.tasks.size()) return nullptr;
    for (std::size_t index = 0U; index < snapshot.task_count; ++index) {
        if (snapshot.tasks[index].instance == instance) return &snapshot.tasks[index];
    }
    return nullptr;
}

[[nodiscard]] bool exact_intent_task(
    const ShellTask& task,
    const ShellActivationIntent& intent) noexcept {
    return task.instance == intent.instance &&
        task.application == intent.application &&
        task.owner == intent.owner &&
        task.root_surface == intent.root_surface;
}

} // namespace

os::core::Result<ShellActivationIntent> make_activation_intent(
    const ShellSnapshot& snapshot) noexcept {
    if (snapshot.revision.value() == 0U ||
        snapshot.task_count > snapshot.tasks.size() ||
        snapshot.view != ShellView::application ||
        snapshot.active_instance.value() == 0U) {
        return shell_error(errors::invalid_activation_intent);
    }

    const ShellTask* task = find_task(snapshot, snapshot.active_instance);
    if (task == nullptr || !task->valid()) {
        return shell_error(errors::invalid_activation_intent);
    }

    ShellActivationIntent intent{
        .shell_revision = snapshot.revision,
        .instance = task->instance,
        .application = task->application,
        .owner = task->owner,
        .root_surface = task->root_surface,
    };
    if (!intent.valid()) return shell_error(errors::invalid_activation_intent);
    return intent;
}

os::core::Result<void> commit_activation_intent(
    const ShellSnapshot& current,
    const ShellActivationIntent& intent,
    ExactActivationBackend backend) noexcept {
    if (!intent.valid() || backend.activate == nullptr ||
        current.revision.value() == 0U || current.task_count > current.tasks.size()) {
        return shell_error(errors::invalid_activation_intent);
    }

    // The current semantic shell state must still be the exact state from which
    // the intent was minted. A lifecycle reconciliation, compositor restart,
    // navigation change, or task identity change invalidates the old commit.
    if (current.revision != intent.shell_revision ||
        current.view != ShellView::application ||
        current.active_instance != intent.instance) {
        return shell_error(errors::stale_activation_intent);
    }

    const ShellTask* task = find_task(current, intent.instance);
    if (task == nullptr || !task->valid() || !exact_intent_task(*task, intent)) {
        return shell_error(errors::stale_activation_intent);
    }

    return backend.activate(backend.context, intent.owner, intent.root_surface);
}

} // namespace os::shell
