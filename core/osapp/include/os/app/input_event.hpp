#pragma once

#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>

namespace os::app {

// Private runtime transport event. The trusted input path may describe one
// already-compositor-authorized surface-local pointer event to the exact
// application process that owns the target. No global display coordinate,
// Linux input identifier or application-chosen target is carried here.
enum class ApplicationPointerPhase : std::uint8_t {
    down = 1U,
    move = 2U,
    up = 3U,
    cancel = 4U,
};

struct ApplicationInputEventV1 final {
    std::uint64_t sequence {0U};
    os::core::PeerIdentity target {};
    std::uint64_t surface_id {0U};
    std::uint64_t frame_sequence {0U};
    std::uint32_t surface_width_px {0U};
    std::uint32_t surface_height_px {0U};
    std::int32_t local_x_px {0};
    std::int32_t local_y_px {0};
    std::uint32_t pointer_id {0U};
    ApplicationPointerPhase phase {ApplicationPointerPhase::down};

    [[nodiscard]] bool valid() const noexcept;
};

inline constexpr std::uint16_t application_input_event_version_v1 = 1U;
inline constexpr std::uint16_t application_input_event_payload_size_v1 = 76U;

// Trusted-runtime send boundary. Callers are expected to have revalidated the
// compositor-issued hit immediately before constructing this event. This codec
// performs bounded structural validation but does not choose a target.
[[nodiscard]] os::core::Result<void> send_application_input_event(
    os::ipc::Channel& channel,
    const ApplicationInputEventV1& event) noexcept;

// Application-side stateful receiver. It binds one event stream to the exact
// bootstrap PeerIdentity and rejects replay/non-monotonic event sequences.
class ApplicationInputEventStream final {
public:
    ApplicationInputEventStream(
        os::ipc::Channel& channel,
        os::core::PeerIdentity expected_identity) noexcept
        : channel_(&channel), expected_identity_(expected_identity) {}

    [[nodiscard]] bool valid() const noexcept {
        return channel_ != nullptr && channel_->valid() &&
            os::core::valid_peer_identity(expected_identity_);
    }

    [[nodiscard]] os::core::Result<ApplicationInputEventV1> receive(
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] std::uint64_t last_sequence() const noexcept { return last_sequence_; }

private:
    os::ipc::Channel* channel_ {nullptr};
    os::core::PeerIdentity expected_identity_ {};
    std::uint64_t last_sequence_ {0U};
};

} // namespace os::app
