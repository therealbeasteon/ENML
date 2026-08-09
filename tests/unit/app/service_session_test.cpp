#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <fcntl.h>
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

    int pipe_fds[2]{-1, -1};
    assert(::pipe2(pipe_fds, O_CLOEXEC) == 0);
    os::core::NativeHandle read_end{pipe_fds[0]};
    os::core::NativeHandle write_end{pipe_fds[1]};

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        read_end.reset();
        write_end.reset();

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
        assert(::read(endpoint.channel.native_fd(), &marker, 1U) == 1);
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

    assert(os::app::send_service_acquire_response(
        pair[0],
        request.value().request_header,
        storage_service,
        8U,
        read_end));

    const std::byte marker{0x5A};
    assert(::write(write_end.native(), &marker, 1U) == 1);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
