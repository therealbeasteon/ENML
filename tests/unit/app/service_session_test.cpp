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

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    // Runtime reacquisition returns a Channel capability, not an arbitrary fd.
    // Use a real AF_UNIX SOCK_SEQPACKET endpoint so Channel::adopt() exercises
    // the same transport contract used by supervised platform services.
    auto endpoint_pair_result = os::ipc::Channel::create_local_pair();
    assert(endpoint_pair_result);
    auto endpoint_pair = std::move(endpoint_pair_result).value();

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        // The child must not succeed through descriptors inherited at fork. Its
        // only usable service endpoint is the SCM_RIGHTS duplicate returned by
        // PlatformServiceSession::acquire().
        endpoint_pair[0].close();
        endpoint_pair[1].close();

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

    const std::byte marker{0x5A};
    assert(::send(endpoint_pair[1].native_fd(), &marker, 1U, MSG_NOSIGNAL) == 1);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
