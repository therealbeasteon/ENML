#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/app/input_event.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>

namespace {

constexpr os::core::PrincipalId app_principal{
    0x415050494E505554ULL,
    0x0000000000000001ULL,
};
constexpr os::core::PrincipalId other_principal{
    0x415050494E504F54ULL,
    0x0000000000000002ULL,
};
constexpr os::core::PeerIdentity app_peer{
    .principal = app_principal,
    .user = os::core::UserId{9U},
    .process = os::core::ProcessId{901U},
};
constexpr os::core::PeerIdentity other_peer{
    .principal = other_principal,
    .user = os::core::UserId{9U},
    .process = os::core::ProcessId{902U},
};

[[nodiscard]] os::app::ApplicationInputEventV1 event_for(
    std::uint64_t sequence,
    os::core::PeerIdentity target = app_peer) {
    return os::app::ApplicationInputEventV1{
        .sequence = sequence,
        .target = target,
        .surface_id = 0x0000000900000001ULL,
        .frame_sequence = 11U,
        .surface_width_px = 240U,
        .surface_height_px = 320U,
        .local_x_px = 73,
        .local_y_px = 99,
        .pointer_id = 0U,
        .phase = os::app::ApplicationPointerPhase::down,
    };
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    os::app::ApplicationInputEventStream stream{pair[1], app_peer};
    assert(stream.valid());
    assert(stream.last_sequence() == 0U);

    auto first = event_for(1U);
    assert(first.valid());
    assert(os::app::send_application_input_event(pair[0], first));

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto received = stream.receive(scratch);
    assert(received);
    assert(received.value().sequence == 1U);
    assert(received.value().target == app_peer);
    assert(received.value().surface_id == first.surface_id);
    assert(received.value().frame_sequence == 11U);
    assert(received.value().local_x_px == 73);
    assert(received.value().local_y_px == 99);
    assert(stream.last_sequence() == 1U);

    auto second = event_for(2U);
    second.phase = os::app::ApplicationPointerPhase::up;
    assert(os::app::send_application_input_event(pair[0], second));
    auto received_second = stream.receive(scratch);
    assert(received_second);
    assert(received_second.value().phase == os::app::ApplicationPointerPhase::up);
    assert(stream.last_sequence() == 2U);

    // Replay/non-monotonic sequence fails closed on the application side.
    assert(os::app::send_application_input_event(pair[0], second));
    auto replay = stream.receive(scratch);
    assert(!replay);
    assert(replay.error().domain == os::core::ErrorDomain::ipc);
    assert(replay.error().code == os::ipc::errors::protocol_violation);
    assert(stream.last_sequence() == 2U);

    // A structurally valid event for another exact runtime identity is not
    // accepted by a stream bound to this application's bootstrap identity.
    auto wrong_target = event_for(3U, other_peer);
    assert(os::app::send_application_input_event(pair[0], wrong_target));
    auto denied = stream.receive(scratch);
    assert(!denied);
    assert(denied.error().domain == os::core::ErrorDomain::security);
    assert(denied.error().code == os::core::errors::security::credential_mismatch);
    assert(stream.last_sequence() == 2U);

    auto invalid_point = event_for(4U);
    invalid_point.local_x_px = 240;
    assert(!invalid_point.valid());
    auto invalid_send = os::app::send_application_input_event(pair[0], invalid_point);
    assert(!invalid_send);
    assert(invalid_send.error().domain == os::core::ErrorDomain::service);
    assert(invalid_send.error().code == os::core::errors::service::invalid_request);

    return 0;
}
