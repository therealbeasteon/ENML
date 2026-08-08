#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/wire.hpp>

namespace {

[[nodiscard]] os::core::ByteSpan bytes_of(const char* text, std::size_t size) {
    return {
        reinterpret_cast<const std::byte*>(text),
        size,
    };
}

[[nodiscard]] os::ipc::WireHeaderV1 make_header(std::uint32_t payload_size) {
    return {
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = os::core::ServiceId{0x0000F001U},
        .operation_id = 2U,
        .request_id = os::core::RequestId{0x1020304050607080ULL},
        .payload_size = payload_size,
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const pid_t child = ::fork();
    assert(child >= 0);

    if (child == 0) {
        pair[0].close();
        constexpr char payload[] = "child-process";
        const auto send_result = pair[1].send(make_header(13U), bytes_of(payload, 13U));
        ::_exit(send_result ? 0 : 91);
    }

    pair[1].close();

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer {};
    auto receive_result = pair[0].receive(receive_buffer);
    assert(receive_result);
    auto message = std::move(receive_result).value();

    assert(message.sender_credentials().process_id == static_cast<std::int64_t>(child));
    assert(message.sender_credentials().user_id == static_cast<std::uint32_t>(::getuid()));
    assert(message.sender_credentials().group_id == static_cast<std::uint32_t>(::getgid()));
    assert(message.payload().size() == 13U);
    assert(std::memcmp(message.payload().data(), "child-process", 13U) == 0);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
