#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/app/service_session.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>

namespace {

constexpr os::core::ServiceId storage_service{0x0000F020U};
constexpr std::uint64_t accessibility_session_id = 0xA110U;

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    // Runtime reacquisition returns Channel capabilities, not arbitrary fd
    // integers. Use real AF_UNIX SOCK_SEQPACKET endpoints so Channel::adopt()
    // exercises the same SCM_RIGHTS transport contract used by the runtime.
    auto endpoint_pair_result = os::ipc::Channel::create_local_pair();
    assert(endpoint_pair_result);
    auto endpoint_pair = std::move(endpoint_pair_result).value();
    auto input_pair_result = os::ipc::Channel::create_local_pair();
    assert(input_pair_result);
    auto input_pair = std::move(input_pair_result).value();
    auto accessibility_pair_result = os::ipc::Channel::create_local_pair();
    assert(accessibility_pair_result);
    auto accessibility_pair = std::move(accessibility_pair_result).value();

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        // The child must not succeed through descriptors inherited at fork. Its
        // usable endpoints arrive only as SCM_RIGHTS duplicates from the
        // authenticated runtime session.
        endpoint_pair[0].close();
        endpoint_pair[1].close();
        input_pair[0].close();
        input_pair[1].close();
        accessibility_pair[0].close();
        accessibility_pair[1].close();

        os::app::PlatformServiceSession session{pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

        // A caller-supplied observed generation is advisory only. The trusted
        // server may return its actual current generation even when lower than
        // the value claimed by the client.
        auto acquired = session.acquire(storage_service, 999U, scratch);
        assert(acquired);
        auto endpoint = std::move(acquired).value();
        assert(endpoint.valid());
        assert(endpoint.service == storage_service);
        assert(endpoint.generation == 8U);

        std::byte marker{};
        assert(::recv(endpoint.channel.native_fd(), &marker, 1U, 0) == 1);
        assert(marker == std::byte{0x5A});

        auto input_events = session.acquire_input_events(scratch);
        assert(input_events);
        auto input_channel = std::move(input_events).value();
        assert(input_channel.valid());
        assert(::recv(input_channel.native_fd(), &marker, 1U, 0) == 1);
        assert(marker == std::byte{0x6B});

        auto accessibility = session.acquire_accessibility(scratch);
        assert(accessibility);
        auto accessibility_endpoint = std::move(accessibility).value();
        assert(accessibility_endpoint.valid());
        assert(accessibility_endpoint.session_id == accessibility_session_id);
        assert(::recv(accessibility_endpoint.channel.native_fd(), &marker, 1U, 0) == 1);
        assert(marker == std::byte{0x7C});
        std::_Exit(0);
    }

    pair[1].close();
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto request = os::app::receive_service_acquire_request(pair[0], scratch);
    assert(request);
    assert(request.value().service == storage_service);
    assert(request.value().known_generation == 999U);
    assert(request.value().sender.process_id == static_cast<std::int64_t>(child));
    assert(request.value().sender.user_id == static_cast<std::uint32_t>(::getuid()));
    assert(request.value().sender.group_id == static_cast<std::uint32_t>(::getgid()));

    auto transfer = endpoint_pair[0].take_native_handle_for_transfer();
    assert(transfer.valid());
    assert(os::app::send_service_acquire_response(
        pair[0],
        request.value().request_header,
        storage_service,
        8U,
        transfer));

    const std::byte service_marker{0x5A};
    assert(::send(endpoint_pair[1].native_fd(), &service_marker, 1U, MSG_NOSIGNAL) == 1);

    auto input_request = os::app::receive_runtime_session_request(pair[0], scratch);
    assert(input_request);
    assert(input_request.value().kind == os::app::RuntimeSessionRequestKind::acquire_input_events);
    assert(input_request.value().service.value() == 0U);
    assert(input_request.value().known_generation == 0U);
    assert(input_request.value().sender.process_id == static_cast<std::int64_t>(child));

    auto input_transfer = input_pair[0].take_native_handle_for_transfer();
    assert(input_transfer.valid());
    assert(os::app::send_input_event_endpoint_response(
        pair[0],
        input_request.value().request_header,
        input_transfer));

    const std::byte input_marker{0x6B};
    assert(::send(input_pair[1].native_fd(), &input_marker, 1U, MSG_NOSIGNAL) == 1);

    auto accessibility_request = os::app::receive_runtime_session_request(pair[0], scratch);
    assert(accessibility_request);
    assert(
        accessibility_request.value().kind ==
        os::app::RuntimeSessionRequestKind::acquire_accessibility);
    assert(accessibility_request.value().service.value() == 0U);
    assert(accessibility_request.value().known_generation == 0U);
    assert(accessibility_request.value().sender.process_id == static_cast<std::int64_t>(child));

    auto accessibility_transfer = accessibility_pair[0].take_native_handle_for_transfer();
    assert(accessibility_transfer.valid());
    assert(os::app::send_accessibility_endpoint_response(
        pair[0],
        accessibility_request.value().request_header,
        accessibility_session_id,
        accessibility_transfer));

    const std::byte accessibility_marker{0x7C};
    assert(::send(
        accessibility_pair[1].native_fd(),
        &accessibility_marker,
        1U,
        MSG_NOSIGNAL) == 1);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
