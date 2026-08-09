#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/display/types.hpp>

namespace os::display {

class Compositor final {
public:
    Compositor(
        DisplayConfiguration configuration,
        TrustedUiPrincipals trusted_principals) noexcept;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const DisplayConfiguration& configuration() const noexcept {
        return configuration_;
    }

    [[nodiscard]] os::core::Result<SurfaceDescriptor> create_surface(
        os::core::PeerIdentity owner,
        const CreateSurfaceRequest& request) noexcept;

    [[nodiscard]] os::core::Result<void> destroy_surface(
        os::core::PeerIdentity caller,
        SurfaceId surface) noexcept;

    [[nodiscard]] os::core::Result<void> set_bounds(
        os::core::PeerIdentity caller,
        SurfaceId surface,
        Rect bounds) noexcept;

    [[nodiscard]] os::core::Result<void> set_visibility(
        os::core::PeerIdentity caller,
        SurfaceId surface,
        SurfaceVisibility visibility) noexcept;

    // Window/application stack authority belongs to trusted system UI. A
    // normal app can update its own pixels and visibility, but cannot promote
    // its root/popup group above another application by choosing a z value.
    [[nodiscard]] os::core::Result<void> activate_application(
        os::core::PeerIdentity caller,
        SurfaceId application_surface) noexcept;

    [[nodiscard]] os::core::Result<FrameReceipt> submit_frame(
        os::core::PeerIdentity caller,
        const FrameSubmission& submission,
        std::uint64_t now_ns) noexcept;

    [[nodiscard]] os::core::Result<SurfaceDescriptor> lookup(
        SurfaceId surface) const noexcept;

    [[nodiscard]] SceneSnapshot scene_snapshot() const noexcept;

    [[nodiscard]] os::core::Result<SurfaceId> hit_test(
        std::int32_t x,
        std::int32_t y) const noexcept;

    void revoke_process(os::core::ProcessId process) noexcept;

    [[nodiscard]] std::size_t surface_count() const noexcept { return surface_count_; }
    [[nodiscard]] std::size_t surface_count_for(
        os::core::PrincipalId principal) const noexcept;

private:
    struct Slot final {
        bool occupied {false};
        SurfaceDescriptor descriptor {};
        std::uint64_t creation_serial {0U};
        std::uint64_t stack_serial {0U};
        std::uint64_t frame_sequence {0U};
        std::uint8_t buffer_slot {0U};
        bool has_frame {false};
    };

    [[nodiscard]] bool role_allowed(
        const os::core::PeerIdentity& owner,
        SurfaceRole role) const noexcept;
    [[nodiscard]] bool bounds_valid(Rect bounds) const noexcept;
    [[nodiscard]] bool damage_valid(
        const SurfaceDescriptor& surface,
        const FrameSubmission& submission) const noexcept;
    [[nodiscard]] bool parent_valid(
        const os::core::PeerIdentity& owner,
        const CreateSurfaceRequest& request) const noexcept;
    [[nodiscard]] bool process_has_application_surface(
        os::core::ProcessId process) const noexcept;
    [[nodiscard]] FrameDeadline deadline_after(std::uint64_t now_ns) const noexcept;
    [[nodiscard]] Slot* find_slot(SurfaceId surface) noexcept;
    [[nodiscard]] const Slot* find_slot(SurfaceId surface) const noexcept;
    [[nodiscard]] os::core::Result<Slot*> find_owned_slot(
        os::core::PeerIdentity caller,
        SurfaceId surface) noexcept;

    DisplayConfiguration configuration_ {};
    TrustedUiPrincipals trusted_principals_ {};
    std::array<Slot, max_surfaces> slots_ {};
    std::uint64_t next_surface_id_ {1U};
    std::uint64_t next_creation_serial_ {1U};
    std::uint64_t next_stack_serial_ {1U};
    std::size_t surface_count_ {0U};
    bool valid_ {false};
};

} // namespace os::display
