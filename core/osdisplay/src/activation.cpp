#include <os/display/compositor.hpp>

#include <cstdint>

#include <os/display/error.hpp>

namespace os::display {

os::core::Result<void> Compositor::activate_application_exact(
    os::core::PeerIdentity caller,
    os::core::PeerIdentity expected_owner,
    SurfaceId application_surface) noexcept {
    if (!os::core::valid_peer_identity(caller) ||
        !os::core::valid_peer_identity(expected_owner)) {
        return display_error(errors::invalid_identity);
    }
    if (caller.principal != trusted_principals_.shell) {
        return display_error(errors::activation_denied);
    }

    Slot* root = find_slot(application_surface);
    if (root == nullptr) return display_error(errors::unknown_surface);

    // The shell must commit the exact lifecycle owner it previously joined to
    // this generation-scoped root. A stale/forged task cannot be redirected to
    // another application's surface merely by retaining or guessing a SurfaceId.
    // Use one generic activation denial for role/owner mismatch so this
    // privileged operation does not become a differential ownership oracle.
    if (root->descriptor.role != SurfaceRole::application ||
        root->descriptor.owner != expected_owner || next_stack_serial_ == 0U) {
        return display_error(errors::activation_denied);
    }

    const std::uint64_t stack_serial = next_stack_serial_;
    ++next_stack_serial_;
    root->stack_serial = stack_serial;
    for (auto& candidate : slots_) {
        if (candidate.occupied && candidate.descriptor.parent == application_surface) {
            // Popups cannot be detached from the activation order of their
            // exact application root.
            candidate.stack_serial = stack_serial;
        }
    }
    return {};
}

} // namespace os::display
