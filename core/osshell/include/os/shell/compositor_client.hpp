#pragma once

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/display/shell_protocol.hpp>
#include <os/display/types.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>
#include <os/shell/activation.hpp>

namespace os::shell {

// Lightweight client half of the private exact-activation transport. The shell
// owns only this client/wire dependency; compositor server/runtime code remains
// outside the shell binary. Possessing the channel is not sufficient authority:
// the compositor server authenticates the live sender on every request.
class ShellCompositorClient final {
public:
    explicit ShellCompositorClient(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    [[nodiscard]] os::core::Result<void> activate_exact(
        os::core::PeerIdentity expected_owner,
        os::display::SurfaceId application_surface,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

// Adapter used by commit_activation_intent(). The scratch buffer is caller
// owned and bounded; no heap allocation, worker, queue or retry loop is added.
struct ShellCompositorActivationContext final {
    ShellCompositorClient* client {nullptr};
    os::core::MutableByteSpan scratch {};

    [[nodiscard]] bool valid() const noexcept {
        return client != nullptr && !scratch.empty();
    }
};

[[nodiscard]] ExactActivationBackend shell_compositor_activation_backend(
    ShellCompositorActivationContext& context) noexcept;

} // namespace os::shell
