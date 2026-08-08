#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/wire.hpp>

namespace {

[[nodiscard]] std::array<std::byte, os::ipc::wire_header_v1_size>
encoded_header(std::uint16_t handle_count) {
    std::array<std::byte, os::ipc::wire_header_v1_size> storage {};
    os::ipc::Encoder encoder {storage};

    std::uint32_t flags = os::ipc::flag_value(os::ipc::WireFlag::request);
    if (handle_count > 0U) {
        flags |= os::ipc::flag_value(os::ipc::WireFlag::has_handles);
    }

    const os::ipc::WireHeaderV1 header {
        .flags = flags,
        .service_id = os::core::ServiceId{0x0000F001U},
        .operation_id = 1U,
        .request_id = os::core::RequestId{1U},
        .payload_size = 0U,
        .handle_count = handle_count,
        .payload_checksum = 0U,
    };
    auto result = os::ipc::encode_wire_header_v1(encoder, header);
    assert(result);
    return storage;
}

[[nodiscard]] std::array<os::ipc::Channel, 2> pair_or_die() {
    auto result = os::ipc::Channel::create_local_pair();
    assert(result);
    return std::move(result).value();
}

void short_packet_rejected() {
    auto pair = pair_or_die();
    std::array<std::byte, 4> garbage {};
    assert(::send(pair[0].native_fd(), garbage.data(), garbage.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(garbage.size()));

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer {};
    auto result = pair[1].receive(receive_buffer);
    assert(!result);
    assert(result.error().code == os::ipc::errors::malformed_message);
}

void oversized_raw_packet_rejected() {
    auto pair = pair_or_die();
    std::vector<std::byte> oversized(os::ipc::max_wire_packet_size + 1U);
    assert(::send(pair[0].native_fd(), oversized.data(), oversized.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(oversized.size()));

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer {};
    auto result = pair[1].receive(receive_buffer);
    assert(!result);
    assert(result.error().code == os::ipc::errors::packet_truncated);
}

void descriptor_count_mismatch_rejected_and_closed() {
    auto pair = pair_or_die();
    const auto header = encoded_header(0U);

    int pipe_descriptors[2] {-1, -1};
    assert(::pipe2(pipe_descriptors, O_CLOEXEC) == 0);
    os::core::NativeHandle read_end {pipe_descriptors[0]};
    os::core::NativeHandle write_end {pipe_descriptors[1]};

    iovec vector {
        .iov_base = const_cast<std::byte*>(header.data()),
        .iov_len = header.size(),
    };
    alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(int))> control_storage {};
    msghdr message {};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control_storage.data();
    message.msg_controllen = control_storage.size();

    cmsghdr* control = CMSG_FIRSTHDR(&message);
    assert(control != nullptr);
    control->cmsg_level = SOL_SOCKET;
    control->cmsg_type = SCM_RIGHTS;
    control->cmsg_len = CMSG_LEN(sizeof(int));
    const int raw_fd = read_end.native();
    std::memcpy(CMSG_DATA(control), &raw_fd, sizeof(raw_fd));

    assert(::sendmsg(pair[0].native_fd(), &message, MSG_NOSIGNAL) ==
           static_cast<ssize_t>(header.size()));

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer {};
    auto result = pair[1].receive(receive_buffer);
    assert(!result);
    assert(result.error().code == os::ipc::errors::handle_count_mismatch);
}

void peer_death_is_reported() {
    auto pair = pair_or_die();
    pair[0].close();

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer {};
    auto result = pair[1].receive(receive_buffer);
    assert(!result);
    assert(result.error().code == os::ipc::errors::peer_died);
}

} // namespace

int main() {
    short_packet_rejected();
    oversized_raw_packet_rejected();
    descriptor_count_mismatch_rejected_and_closed();
    peer_death_is_reported();
    return 0;
}
