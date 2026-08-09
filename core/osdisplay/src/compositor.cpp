#include <os/display/compositor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/display/error.hpp>

namespace os::display {
namespace {

[[nodiscard]] constexpr std::uint8_t role_band(SurfaceRole role) noexcept {
    switch (role) {
    case SurfaceRole::application:
    case SurfaceRole::popup:
        return 1U;
    case SurfaceRole::system_chrome:
        return 2U;
    case SurfaceRole::secure_system:
        return 3U;
    }
    return 0U;
}

[[nodiscard]] constexpr std::uint8_t role_rank_within_band(SurfaceRole role) noexcept {
    return role == SurfaceRole::popup ? 2U : 1U;
}

[[nodiscard]] constexpr bool same_owner(
    const os::core::PeerIdentity& left,
    const os::core::PeerIdentity& right) noexcept {
    return left == right;
}

[[nodiscard]] constexpr bool point_inside(Rect bounds, std::int32_t x, std::int32_t y) noexcept {
    if (x < bounds.x || y < bounds.y) return false;
    const auto right = static_cast<std::int64_t>(bounds.x) + static_cast<std::int64_t>(bounds.width);
    const auto bottom = static_cast<std::int64_t>(bounds.y) + static_cast<std::int64_t>(bounds.height);
    return static_cast<std::int64_t>(x) < right && static_cast<std::int64_t>(y) < bottom;
}

} // namespace

Compositor::Compositor(
    DisplayConfiguration configuration,
    TrustedUiPrincipals trusted_principals) noexcept
    : configuration_(configuration), trusted_principals_(trusted_principals) {
    if (!configuration_.size.valid() || configuration_.refresh_millihz == 0U ||
        !os::core::valid_principal(trusted_principals_.shell) ||
        !os::core::valid_principal(trusted_principals_.secure_ui) ||
        trusted_principals_.shell == trusted_principals_.secure_ui) {
        return;
    }
    const auto horizontal = static_cast<std::uint64_t>(configuration_.safe_insets.left) +
        static_cast<std::uint64_t>(configuration_.safe_insets.right);
    const auto vertical = static_cast<std::uint64_t>(configuration_.safe_insets.top) +
        static_cast<std::uint64_t>(configuration_.safe_insets.bottom);
    if (horizontal >= static_cast<std::uint64_t>(configuration_.size.width) ||
        vertical >= static_cast<std::uint64_t>(configuration_.size.height)) return;

    constexpr std::uint64_t ns_per_millihertz_period = 1'000'000'000'000ULL;
    const auto interval = ns_per_millihertz_period /
        static_cast<std::uint64_t>(configuration_.refresh_millihz);
    if (interval == 0U || configuration_.compositor_margin_ns >= interval) return;
    valid_ = true;
}

bool Compositor::role_allowed(const os::core::PeerIdentity& owner, SurfaceRole role) const noexcept {
    switch (role) {
    case SurfaceRole::application:
    case SurfaceRole::popup:
        return true;
    case SurfaceRole::system_chrome:
        return owner.principal == trusted_principals_.shell;
    case SurfaceRole::secure_system:
        return owner.principal == trusted_principals_.secure_ui;
    }
    return false;
}

bool Compositor::bounds_valid(Rect bounds) const noexcept {
    if (!valid_ || !bounds.nonempty() || bounds.x < 0 || bounds.y < 0) return false;
    const auto right = static_cast<std::uint64_t>(static_cast<std::uint32_t>(bounds.x)) +
        static_cast<std::uint64_t>(bounds.width);
    const auto bottom = static_cast<std::uint64_t>(static_cast<std::uint32_t>(bounds.y)) +
        static_cast<std::uint64_t>(bounds.height);
    return right <= static_cast<std::uint64_t>(configuration_.size.width) &&
        bottom <= static_cast<std::uint64_t>(configuration_.size.height);
}

bool Compositor::parent_valid(
    const os::core::PeerIdentity& owner,
    const CreateSurfaceRequest& request) const noexcept {
    if (request.role != SurfaceRole::popup) return request.parent.value() == 0U;
    const Slot* parent = find_slot(request.parent);
    return parent != nullptr && same_owner(parent->descriptor.owner, owner) &&
        parent->descriptor.role == SurfaceRole::application;
}

bool Compositor::process_has_application_surface(os::core::ProcessId process) const noexcept {
    if (process.value() == 0U) return false;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.descriptor.owner.process == process &&
            slot.descriptor.role == SurfaceRole::application) return true;
    }
    return false;
}

os::core::Result<SurfaceDescriptor> Compositor::create_surface(
    os::core::PeerIdentity owner,
    const CreateSurfaceRequest& request) noexcept {
    if (!valid_) return display_error(errors::invalid_configuration);
    if (!os::core::valid_peer_identity(owner)) return display_error(errors::invalid_identity);
    if (!role_allowed(owner, request.role)) return display_error(errors::invalid_role);
    if (!bounds_valid(request.bounds)) return display_error(errors::invalid_geometry);
    if (!parent_valid(owner, request)) return display_error(errors::invalid_parent);
    if (request.role == SurfaceRole::application && process_has_application_surface(owner.process)) {
        return display_error(errors::application_surface_exists);
    }
    if (surface_count_ >= max_surfaces) return display_error(errors::surface_limit);
    if (surface_count_for(owner.principal) >= max_surfaces_per_principal) {
        return display_error(errors::principal_surface_limit);
    }
    if (next_surface_id_ == 0U || next_creation_serial_ == 0U ||
        (request.role == SurfaceRole::application && next_stack_serial_ == 0U)) {
        return display_error(errors::surface_id_exhausted);
    }

    Slot* available = nullptr;
    for (auto& slot : slots_) {
        if (!slot.occupied) {
            available = &slot;
            break;
        }
    }
    if (available == nullptr) return display_error(errors::surface_limit);

    const SurfaceId id{next_surface_id_};
    ++next_surface_id_;
    const std::uint64_t creation_serial = next_creation_serial_;
    ++next_creation_serial_;
    std::uint64_t stack_serial = 0U;
    if (request.role == SurfaceRole::application) {
        stack_serial = next_stack_serial_;
        ++next_stack_serial_;
    } else if (request.role == SurfaceRole::popup) {
        const Slot* parent = find_slot(request.parent);
        if (parent == nullptr) return display_error(errors::invalid_parent);
        stack_serial = parent->stack_serial;
    }

    available->occupied = true;
    available->descriptor = {
        .id = id,
        .owner = owner,
        .role = request.role,
        .parent = request.parent,
        .bounds = request.bounds,
        .visibility = SurfaceVisibility::hidden,
        .accepts_input = request.accepts_input,
    };
    available->creation_serial = creation_serial;
    available->stack_serial = stack_serial;
    ++surface_count_;
    return available->descriptor;
}

Compositor::Slot* Compositor::find_slot(SurfaceId surface) noexcept {
    if (surface.value() == 0U) return nullptr;
    for (auto& slot : slots_) if (slot.occupied && slot.descriptor.id == surface) return &slot;
    return nullptr;
}

const Compositor::Slot* Compositor::find_slot(SurfaceId surface) const noexcept {
    if (surface.value() == 0U) return nullptr;
    for (const auto& slot : slots_) if (slot.occupied && slot.descriptor.id == surface) return &slot;
    return nullptr;
}

os::core::Result<Compositor::Slot*> Compositor::find_owned_slot(
    os::core::PeerIdentity caller,
    SurfaceId surface) noexcept {
    if (!os::core::valid_peer_identity(caller)) return display_error(errors::invalid_identity);
    Slot* slot = find_slot(surface);
    if (slot == nullptr) return display_error(errors::unknown_surface);
    if (!same_owner(slot->descriptor.owner, caller)) return display_error(errors::owner_mismatch);
    return slot;
}

os::core::Result<void> Compositor::destroy_surface(
    os::core::PeerIdentity caller,
    SurfaceId surface) noexcept {
    auto owned = find_owned_slot(caller, surface);
    if (!owned) return owned.error();
    for (auto& candidate : slots_) {
        if (candidate.occupied && candidate.descriptor.parent == surface) {
            candidate = Slot{};
            --surface_count_;
        }
    }
    *owned.value() = Slot{};
    --surface_count_;
    return {};
}

os::core::Result<void> Compositor::set_bounds(
    os::core::PeerIdentity caller,
    SurfaceId surface,
    Rect bounds) noexcept {
    if (!bounds_valid(bounds)) return display_error(errors::invalid_geometry);
    auto owned = find_owned_slot(caller, surface);
    if (!owned) return owned.error();
    owned.value()->descriptor.bounds = bounds;
    owned.value()->buffer = {};
    owned.value()->has_frame = false;
    return {};
}

os::core::Result<void> Compositor::set_visibility(
    os::core::PeerIdentity caller,
    SurfaceId surface,
    SurfaceVisibility visibility) noexcept {
    auto owned = find_owned_slot(caller, surface);
    if (!owned) return owned.error();
    owned.value()->descriptor.visibility = visibility;
    return {};
}

os::core::Result<void> Compositor::activate_application(
    os::core::PeerIdentity caller,
    SurfaceId application_surface) noexcept {
    if (!os::core::valid_peer_identity(caller)) return display_error(errors::invalid_identity);
    if (caller.principal != trusted_principals_.shell) return display_error(errors::activation_denied);
    Slot* root = find_slot(application_surface);
    if (root == nullptr) return display_error(errors::unknown_surface);
    if (root->descriptor.role != SurfaceRole::application || next_stack_serial_ == 0U) {
        return display_error(errors::activation_denied);
    }
    const std::uint64_t stack_serial = next_stack_serial_;
    ++next_stack_serial_;
    root->stack_serial = stack_serial;
    for (auto& candidate : slots_) {
        if (candidate.occupied && candidate.descriptor.parent == application_surface) {
            candidate.stack_serial = stack_serial;
        }
    }
    return {};
}

bool Compositor::damage_valid(
    const SurfaceDescriptor& surface,
    const FrameSubmission& submission) const noexcept {
    if (submission.damage_count > max_damage_rectangles) return false;
    for (std::size_t index = 0U; index < submission.damage_count; ++index) {
        const Rect damage = submission.damage[index];
        if (!damage.nonempty() || damage.x < 0 || damage.y < 0) return false;
        const auto right = static_cast<std::uint64_t>(static_cast<std::uint32_t>(damage.x)) +
            static_cast<std::uint64_t>(damage.width);
        const auto bottom = static_cast<std::uint64_t>(static_cast<std::uint32_t>(damage.y)) +
            static_cast<std::uint64_t>(damage.height);
        if (right > static_cast<std::uint64_t>(surface.bounds.width) ||
            bottom > static_cast<std::uint64_t>(surface.bounds.height)) return false;
    }
    return true;
}

FrameDeadline Compositor::deadline_after(std::uint64_t now_ns) const noexcept {
    constexpr std::uint64_t ns_per_millihertz_period = 1'000'000'000'000ULL;
    const auto interval = ns_per_millihertz_period /
        static_cast<std::uint64_t>(configuration_.refresh_millihz);
    const auto periods = now_ns / interval;
    std::uint64_t next_vsync = std::numeric_limits<std::uint64_t>::max();
    if (periods < std::numeric_limits<std::uint64_t>::max() / interval) {
        next_vsync = (periods + 1U) * interval;
    }
    const auto deadline = next_vsync > configuration_.compositor_margin_ns
        ? next_vsync - configuration_.compositor_margin_ns : 0U;
    return {.next_vsync_ns = next_vsync, .submission_deadline_ns = deadline};
}

os::core::Result<FrameReceipt> Compositor::submit_frame(
    os::core::PeerIdentity caller,
    const FrameSubmission& submission,
    std::uint64_t now_ns) noexcept {
    auto owned = find_owned_slot(caller, submission.surface);
    if (!owned) return owned.error();
    Slot* slot = owned.value();
    if (submission.buffer.value() == 0U) return display_error(errors::invalid_buffer);
    if (submission.sequence == 0U || submission.sequence <= slot->frame_sequence) {
        return display_error(errors::frame_replay);
    }
    if (submission.buffer_slot >= max_frame_buffer_slots) {
        return display_error(errors::invalid_buffer_slot);
    }
    if (!damage_valid(slot->descriptor, submission)) return display_error(errors::invalid_damage);

    slot->buffer = submission.buffer;
    slot->frame_sequence = submission.sequence;
    slot->buffer_slot = submission.buffer_slot;
    slot->has_frame = true;
    return FrameReceipt{
        .surface = submission.surface,
        .buffer = submission.buffer,
        .sequence = submission.sequence,
        .deadline = deadline_after(now_ns),
    };
}

os::core::Result<SurfaceDescriptor> Compositor::lookup(SurfaceId surface) const noexcept {
    const Slot* slot = find_slot(surface);
    if (slot == nullptr) return display_error(errors::unknown_surface);
    return slot->descriptor;
}

SceneSnapshot Compositor::scene_snapshot() const noexcept {
    struct OrderedSlot final { const Slot* slot {nullptr}; };
    std::array<OrderedSlot, max_surfaces> ordered{};
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied) {
            ordered[count] = {&slot};
            ++count;
        }
    }

    const auto after = [](const Slot& left, const Slot& right) noexcept {
        const auto left_band = role_band(left.descriptor.role);
        const auto right_band = role_band(right.descriptor.role);
        if (left_band != right_band) return left_band > right_band;
        if (left_band == 1U && left.stack_serial != right.stack_serial) {
            return left.stack_serial > right.stack_serial;
        }
        const auto left_rank = role_rank_within_band(left.descriptor.role);
        const auto right_rank = role_rank_within_band(right.descriptor.role);
        if (left_rank != right_rank) return left_rank > right_rank;
        return left.creation_serial > right.creation_serial;
    };

    for (std::size_t index = 1U; index < count; ++index) {
        const OrderedSlot current = ordered[index];
        std::size_t position = index;
        while (position > 0U && after(*ordered[position - 1U].slot, *current.slot)) {
            ordered[position] = ordered[position - 1U];
            --position;
        }
        ordered[position] = current;
    }

    SceneSnapshot snapshot;
    snapshot.count = count;
    for (std::size_t index = 0U; index < count; ++index) {
        const Slot& slot = *ordered[index].slot;
        snapshot.entries[index] = {
            .surface = slot.descriptor,
            .buffer = slot.buffer,
            .frame_sequence = slot.frame_sequence,
            .buffer_slot = slot.buffer_slot,
            .has_frame = slot.has_frame,
            .capture_allowed = slot.descriptor.role != SurfaceRole::secure_system,
        };
    }
    return snapshot;
}

os::core::Result<SurfaceId> Compositor::hit_test(std::int32_t x, std::int32_t y) const noexcept {
    if (!valid_) return display_error(errors::invalid_configuration);
    const auto snapshot = scene_snapshot();
    for (std::size_t offset = 0U; offset < snapshot.count; ++offset) {
        const std::size_t index = snapshot.count - 1U - offset;
        const auto& entry = snapshot.entries[index];
        if (entry.surface.visibility != SurfaceVisibility::visible ||
            !entry.surface.accepts_input || !entry.has_frame) continue;
        if (point_inside(entry.surface.bounds, x, y)) return entry.surface.id;
    }
    return display_error(errors::unknown_surface);
}

void Compositor::invalidate_buffer(BufferId buffer) noexcept {
    if (buffer.value() == 0U) return;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.buffer == buffer) {
            slot.buffer = {};
            slot.has_frame = false;
        }
    }
}

void Compositor::revoke_process(os::core::ProcessId process) noexcept {
    if (process.value() == 0U) return;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.descriptor.owner.process == process) {
            slot = Slot{};
            --surface_count_;
        }
    }
}

std::size_t Compositor::surface_count_for(os::core::PrincipalId principal) const noexcept {
    if (!os::core::valid_principal(principal)) return 0U;
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.descriptor.owner.principal == principal) ++count;
    }
    return count;
}

} // namespace os::display
