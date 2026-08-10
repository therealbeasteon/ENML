#include <os/shell/preview_policy.hpp>

#include <cstddef>

#include <os/shell/error.hpp>

namespace os::shell {
namespace {

[[nodiscard]] const ShellTask* find_task(
    const ShellSnapshot& shell,
    os::core::ApplicationInstanceId instance) noexcept {
    if (shell.task_count > shell.tasks.size()) return nullptr;
    for (std::size_t index = 0U; index < shell.task_count; ++index) {
        if (shell.tasks[index].instance == instance) return &shell.tasks[index];
    }
    return nullptr;
}

[[nodiscard]] const os::display::SceneEntry* find_scene_entry(
    const os::display::SceneSnapshot& scene,
    os::display::SurfaceId surface) noexcept {
    if (scene.count > scene.entries.size()) return nullptr;
    for (std::size_t index = 0U; index < scene.count; ++index) {
        if (scene.entries[index].surface.id == surface) return &scene.entries[index];
    }
    return nullptr;
}

[[nodiscard]] bool preview_entry_allowed(
    const ShellTask& task,
    const os::display::SceneEntry& entry) noexcept {
    return task.valid() && entry.surface.valid() &&
        entry.surface.id == task.root_surface &&
        entry.surface.owner == task.owner &&
        entry.surface.role == os::display::SurfaceRole::application &&
        entry.surface.parent.value() == 0U &&
        entry.surface.visibility == os::display::SurfaceVisibility::visible &&
        entry.trusted_presentation == os::display::TrustedPresentation::none &&
        entry.capture_allowed && entry.has_frame &&
        os::display::valid_display_object_value(entry.buffer.value()) &&
        entry.frame_sequence != 0U;
}

} // namespace

os::core::Result<TaskPreviewGrant> authorize_task_preview(
    const ShellSnapshot& shell,
    const os::display::SceneSnapshot& scene,
    os::core::ApplicationInstanceId instance) noexcept {
    if (shell.revision.value() == 0U || shell.task_count > shell.tasks.size() ||
        scene.count > scene.entries.size() || instance.value() == 0U) {
        return shell_error(errors::preview_capture_denied);
    }

    const ShellTask* task = find_task(shell, instance);
    if (task == nullptr || !task->valid()) {
        return shell_error(errors::preview_capture_denied);
    }
    const os::display::SceneEntry* entry = find_scene_entry(scene, task->root_surface);
    if (entry == nullptr || !preview_entry_allowed(*task, *entry)) {
        return shell_error(errors::preview_capture_denied);
    }

    TaskPreviewGrant grant{
        .shell_revision = shell.revision,
        .instance = task->instance,
        .owner = task->owner,
        .root_surface = task->root_surface,
        .buffer = entry->buffer,
        .frame_sequence = entry->frame_sequence,
    };
    if (!grant.valid()) return shell_error(errors::preview_capture_denied);
    return grant;
}

os::core::Result<void> validate_task_preview_grant(
    const ShellSnapshot& shell,
    const os::display::SceneSnapshot& scene,
    const TaskPreviewGrant& grant) noexcept {
    if (!grant.valid() || shell.revision.value() == 0U ||
        shell.task_count > shell.tasks.size() || scene.count > scene.entries.size()) {
        return shell_error(errors::stale_preview_grant);
    }
    if (shell.revision != grant.shell_revision) {
        return shell_error(errors::stale_preview_grant);
    }

    const ShellTask* task = find_task(shell, grant.instance);
    if (task == nullptr || !task->valid() || task->owner != grant.owner ||
        task->root_surface != grant.root_surface) {
        return shell_error(errors::stale_preview_grant);
    }

    const os::display::SceneEntry* entry = find_scene_entry(scene, grant.root_surface);
    if (entry == nullptr || !preview_entry_allowed(*task, *entry) ||
        entry->buffer != grant.buffer || entry->frame_sequence != grant.frame_sequence) {
        return shell_error(errors::stale_preview_grant);
    }
    return {};
}

} // namespace os::shell
