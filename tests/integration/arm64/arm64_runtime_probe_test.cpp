#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <fcntl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/wire.hpp>

#if !defined(__aarch64__)
#error "arm64_runtime_probe_test must be compiled for AArch64"
#endif

int main() {
    static_assert(sizeof(void*) == 8U);

    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = os::core::ServiceId{0x0000F001U},
        .operation_id = 1U,
        .request_id = os::core::RequestId{0x0102030405060708ULL},
        .payload_size = 5U,
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    constexpr std::array<std::uint8_t, os::ipc::wire_header_v1_size> golden{
        0x4F,0x53,0x49,0x50,0x28,0x00,0x01,0x00,
        0x01,0x00,0x00,0x00,0x01,0xF0,0x00,0x00,
        0x01,0x00,0x00,0x00,0x08,0x07,0x06,0x05,
        0x04,0x03,0x02,0x01,0x05,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    };
    std::array<std::byte, os::ipc::wire_header_v1_size> encoded{};
    os::ipc::Encoder encoder{encoded};
    assert(os::ipc::encode_wire_header_v1(encoder, header));
    assert(std::memcmp(encoded.data(), golden.data(), golden.size()) == 0);

    // Exercise the Linux transport and SCM_RIGHTS on the AArch64 executable,
    // not merely its serializer.
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();
    int pipe_fds[2]{-1, -1};
    assert(::pipe2(pipe_fds, O_CLOEXEC) == 0);
    os::core::NativeHandle read_end{pipe_fds[0]};
    os::core::NativeHandle write_end{pipe_fds[1]};

    const os::ipc::WireHeaderV1 handle_header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request) |
                 os::ipc::flag_value(os::ipc::WireFlag::has_handles),
        .service_id = os::core::ServiceId{0x0000F001U},
        .operation_id = 2U,
        .request_id = os::core::RequestId{9U},
        .payload_size = 0U,
        .handle_count = 1U,
        .payload_checksum = 0U,
    };
    const std::span<const os::core::NativeHandle> handles{&read_end, 1U};
    assert(pair[0].send(handle_header, {}, handles));
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    auto message_result = pair[1].receive(receive_buffer);
    assert(message_result);
    auto message = std::move(message_result).value();
    assert(message.handle_count() == 1U);
    assert(message.sender_credentials().process_id == static_cast<std::int64_t>(::getpid()));
    assert((::fcntl(message.handles()[0].native(), F_GETFD) & FD_CLOEXEC) != 0);

    const char marker = 'A';
    assert(::write(write_end.native(), &marker, 1U) == 1);
    char observed = '\0';
    assert(::read(message.handles()[0].native(), &observed, 1U) == 1);
    assert(observed == marker);

    utsname info{};
    assert(::uname(&info) == 0);
    return 0;
}
