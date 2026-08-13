#include <os/ipc/channel.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::ipc {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool is_peer_death_errno(int value) noexcept {
    return value == EPIPE || value == ECONNRESET || value == ENOTCONN;
}

[[nodiscard]] os::core::Result<void> set_close_on_exec(int fd) noexcept {
    int flags = -1;
    do {
        flags = ::fcntl(fd, F_GETFD);
    } while (flags < 0 && errno == EINTR);

    if (flags < 0) {
        return ipc_error(errors::transport_failure);
    }

    int result = -1;
    do {
        result = ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        return ipc_error(errors::transport_failure);
    }
    return {};
}

struct AncillaryData final {
    std::array<os::core::NativeHandle, max_handle_count> handles {};
    std::uint16_t handle_count {0};
    KernelPeerCredentials credentials {};
    bool has_credentials {false};
};

// Closes descriptors in [from, count) that this process will not adopt.
//
// By the time a SCM_RIGHTS record is visible here the kernel has already
// installed every descriptor it carries into this process. Rejecting the record
// therefore does not undo the installation: any descriptor left unadopted stays
// open with no owner. Closing them is what keeps a peer from walking the process
// out of its descriptor table by repeatedly sending records we refuse.
void close_unadopted_descriptors(
    const std::byte* descriptor_bytes,
    std::size_t from,
    std::size_t count) noexcept {
    for (std::size_t index = from; index < count; ++index) {
        int fd = -1;
        std::memcpy(&fd, descriptor_bytes + (index * sizeof(int)), sizeof(fd));
        if (fd >= 0) {
            ::close(fd);
        }
    }
}

[[nodiscard]] os::core::Result<void>
append_rights(AncillaryData& output, const cmsghdr& control) noexcept {
    if (control.cmsg_len < CMSG_LEN(0)) {
        return ipc_error(errors::invalid_ancillary);
    }

    const auto payload_bytes = control.cmsg_len - CMSG_LEN(0);
    const auto* descriptor_bytes =
        reinterpret_cast<const std::byte*>(CMSG_DATA(const_cast<cmsghdr*>(&control)));

    // A trailing partial descriptor cannot be interpreted, but the whole ones
    // preceding it are still installed and still need closing.
    const auto descriptor_count = payload_bytes / sizeof(int);
    if (payload_bytes == 0U || (payload_bytes % sizeof(int)) != 0U) {
        close_unadopted_descriptors(descriptor_bytes, 0U, descriptor_count);
        return ipc_error(errors::invalid_ancillary);
    }

    const auto available = static_cast<std::size_t>(max_handle_count - output.handle_count);
    if (descriptor_count > available) {
        close_unadopted_descriptors(descriptor_bytes, 0U, descriptor_count);
        return ipc_error(errors::ancillary_truncated);
    }

    for (std::size_t index = 0; index < descriptor_count; ++index) {
        int fd = -1;
        std::memcpy(
            &fd,
            descriptor_bytes + (index * sizeof(int)),
            sizeof(fd));

        if (fd < 0) {
            close_unadopted_descriptors(descriptor_bytes, index + 1U, descriptor_count);
            return ipc_error(errors::invalid_native_handle);
        }

        auto cloexec_result = set_close_on_exec(fd);
        if (!cloexec_result) {
            ::close(fd);
            close_unadopted_descriptors(descriptor_bytes, index + 1U, descriptor_count);
            return cloexec_result.error();
        }

        // Adopting into NativeHandle transfers ownership: descriptors already in
        // `output` are closed by AncillaryData's destructor when a later control
        // record in the same message is rejected.
        output.handles[output.handle_count] = os::core::NativeHandle{fd};
        ++output.handle_count;
    }

    return {};
}

[[nodiscard]] os::core::Result<void>
read_credentials(AncillaryData& output, const cmsghdr& control) noexcept {
    if (output.has_credentials || control.cmsg_len != CMSG_LEN(sizeof(ucred))) {
        return ipc_error(errors::invalid_ancillary);
    }

    ucred credentials {};
    std::memcpy(&credentials, CMSG_DATA(const_cast<cmsghdr*>(&control)), sizeof(credentials));

    if (credentials.pid <= 0) {
        return ipc_error(errors::invalid_ancillary);
    }

    output.credentials = KernelPeerCredentials{
        .process_id = static_cast<std::int64_t>(credentials.pid),
        .user_id = static_cast<std::uint32_t>(credentials.uid),
        .group_id = static_cast<std::uint32_t>(credentials.gid),
    };
    output.has_credentials = true;
    return {};
}

[[nodiscard]] os::core::Result<AncillaryData>
parse_ancillary(msghdr& message) noexcept {
    AncillaryData output {};

    for (cmsghdr* control = CMSG_FIRSTHDR(&message);
         control != nullptr;
         control = CMSG_NXTHDR(&message, control)) {
        if (control->cmsg_level != SOL_SOCKET) {
            return ipc_error(errors::invalid_ancillary);
        }

        os::core::Result<void> parse_result {};
        if (control->cmsg_type == SCM_RIGHTS) {
            parse_result = append_rights(output, *control);
        } else if (control->cmsg_type == SCM_CREDENTIALS) {
            parse_result = read_credentials(output, *control);
        } else {
            return ipc_error(errors::invalid_ancillary);
        }

        if (!parse_result) {
            return parse_result.error();
        }
    }

    if ((message.msg_flags & MSG_CTRUNC) != 0) {
        return ipc_error(errors::ancillary_truncated);
    }
    if (!output.has_credentials) {
        return ipc_error(errors::missing_credentials);
    }

    return output;
}

[[nodiscard]] os::core::Result<void>
validate_send_shape(
    const WireHeaderV1& header,
    os::core::ByteSpan payload,
    std::span<const os::core::NativeHandle> handles) noexcept {
    if (payload.size() > max_inline_payload_size ||
        handles.size() > static_cast<std::size_t>(max_handle_count)) {
        return ipc_error(errors::oversized_message);
    }

    if (header.payload_size != payload.size()) {
        return ipc_error(errors::packet_size_mismatch);
    }
    if (header.handle_count != handles.size()) {
        return ipc_error(errors::handle_count_mismatch);
    }

    for (const auto& handle : handles) {
        if (!handle.valid()) {
            return ipc_error(errors::invalid_native_handle);
        }
    }
    return {};
}

} // namespace

InboundMessage::InboundMessage(
    WireHeaderV1 header,
    os::core::ByteSpan payload,
    std::array<os::core::NativeHandle, max_handle_count>&& handles,
    std::uint16_t handle_count,
    KernelPeerCredentials sender) noexcept
    : header_(header),
      payload_(payload),
      handles_(std::move(handles)),
      handle_count_(handle_count),
      sender_(sender) {}

os::core::Result<os::core::NativeHandle>
InboundMessage::take_handle(std::uint16_t index) noexcept {
    if (index >= handle_count_ || !handles_[index].valid()) {
        return ipc_error(errors::invalid_native_handle);
    }
    return std::move(handles_[index]);
}

Channel::Channel(os::core::NativeHandle handle) noexcept
    : handle_(std::move(handle)) {}

os::core::Result<void> Channel::enable_kernel_credentials() noexcept {
    if (!handle_.valid()) {
        return ipc_error(errors::invalid_channel);
    }

    const int enabled = 1;
    if (::setsockopt(
            handle_.native(),
            SOL_SOCKET,
            SO_PASSCRED,
            &enabled,
            static_cast<socklen_t>(sizeof(enabled))) != 0) {
        return ipc_error(errors::transport_failure);
    }
    return {};
}

os::core::Result<std::array<Channel, 2>> Channel::create_local_pair() noexcept {
    int descriptors[2] {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, descriptors) != 0) {
        return ipc_error(errors::transport_failure);
    }

    Channel left {os::core::NativeHandle{descriptors[0]}};
    Channel right {os::core::NativeHandle{descriptors[1]}};

    auto result = left.enable_kernel_credentials();
    if (!result) {
        return result.error();
    }
    result = right.enable_kernel_credentials();
    if (!result) {
        return result.error();
    }

    return std::array<Channel, 2>{std::move(left), std::move(right)};
}

os::core::Result<Channel> Channel::adopt(os::core::NativeHandle handle) noexcept {
    if (!handle.valid()) {
        return ipc_error(errors::invalid_channel);
    }

    Channel channel {std::move(handle)};
    auto result = channel.enable_kernel_credentials();
    if (!result) {
        return result.error();
    }
    return channel;
}

os::core::Result<void>
Channel::send(
    const WireHeaderV1& header,
    os::core::ByteSpan payload,
    std::span<const os::core::NativeHandle> handles) noexcept {
    if (!handle_.valid()) {
        return ipc_error(errors::invalid_channel);
    }

    auto shape_result = validate_send_shape(header, payload, handles);
    if (!shape_result) {
        return shape_result.error();
    }

    std::array<std::byte, wire_header_v1_size> header_storage {};
    Encoder encoder {header_storage};
    auto encode_result = encode_wire_header_v1(encoder, header);
    if (!encode_result) {
        return encode_result.error();
    }

    std::array<iovec, 2> vectors {};
    vectors[0].iov_base = header_storage.data();
    vectors[0].iov_len = header_storage.size();
    vectors[1].iov_base = const_cast<std::byte*>(payload.data());
    vectors[1].iov_len = payload.size();

    alignas(cmsghdr)
    std::array<std::byte, CMSG_SPACE(sizeof(int) * max_handle_count)> control_storage {};

    msghdr message {};
    message.msg_iov = vectors.data();
    message.msg_iovlen = payload.empty() ? 1U : 2U;

    std::array<int, max_handle_count> native_handles {};
    if (!handles.empty()) {
        for (std::size_t index = 0; index < handles.size(); ++index) {
            native_handles[index] = handles[index].native();
        }

        message.msg_control = control_storage.data();
        message.msg_controllen = CMSG_SPACE(sizeof(int) * handles.size());

        cmsghdr* control = CMSG_FIRSTHDR(&message);
        if (control == nullptr) {
            return ipc_error(errors::transport_failure);
        }
        control->cmsg_level = SOL_SOCKET;
        control->cmsg_type = SCM_RIGHTS;
        control->cmsg_len = CMSG_LEN(sizeof(int) * handles.size());
        std::memcpy(CMSG_DATA(control), native_handles.data(), sizeof(int) * handles.size());
    }

    const auto total_size = header_storage.size() + payload.size();
    ssize_t sent = -1;
    do {
        sent = ::sendmsg(handle_.native(), &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);

    if (sent < 0) {
        if (is_peer_death_errno(errno)) {
            return ipc_error(errors::peer_died);
        }
        return ipc_error(errors::transport_failure);
    }

    if (static_cast<std::size_t>(sent) != total_size) {
        return ipc_error(errors::transport_failure);
    }
    return {};
}

os::core::Result<InboundMessage>
Channel::receive(os::core::MutableByteSpan receive_buffer) noexcept {
    if (!handle_.valid()) {
        return ipc_error(errors::invalid_channel);
    }
    if (receive_buffer.size() < max_wire_packet_size) {
        return ipc_error(errors::buffer_too_small);
    }

    iovec vector {
        .iov_base = receive_buffer.data(),
        .iov_len = max_wire_packet_size,
    };

    alignas(cmsghdr)
    std::array<std::byte,
        CMSG_SPACE(sizeof(int) * max_handle_count) + CMSG_SPACE(sizeof(ucred))>
        control_storage {};

    msghdr message {};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control_storage.data();
    message.msg_controllen = control_storage.size();

    ssize_t received = -1;
    do {
        received = ::recvmsg(
            handle_.native(),
            &message,
            MSG_CMSG_CLOEXEC | MSG_TRUNC);
    } while (received < 0 && errno == EINTR);

    if (received == 0) {
        return ipc_error(errors::peer_died);
    }
    if (received < 0) {
        if (is_peer_death_errno(errno)) {
            return ipc_error(errors::peer_died);
        }
        return ipc_error(errors::transport_failure);
    }

    auto ancillary_result = parse_ancillary(message);
    if (!ancillary_result) {
        return ancillary_result.error();
    }
    auto ancillary = std::move(ancillary_result).value();

    const auto received_size = static_cast<std::size_t>(received);
    if ((message.msg_flags & MSG_TRUNC) != 0 || received_size > max_wire_packet_size) {
        return ipc_error(errors::packet_truncated);
    }
    if (received_size < wire_header_v1_size) {
        return ipc_error(errors::malformed_message);
    }

    Decoder decoder {os::core::ByteSpan{receive_buffer.data(), received_size}};
    auto header_result = decode_wire_header_v1(decoder);
    if (!header_result) {
        return header_result.error();
    }
    auto header = header_result.value();

    auto payload_result = decoder.read_raw(header.payload_size);
    if (!payload_result) {
        return ipc_error(errors::packet_size_mismatch);
    }
    auto end_result = decoder.require_end();
    if (!end_result) {
        return ipc_error(errors::packet_size_mismatch);
    }

    if (header.handle_count != ancillary.handle_count) {
        return ipc_error(errors::handle_count_mismatch);
    }

    return InboundMessage{
        header,
        payload_result.value(),
        std::move(ancillary.handles),
        ancillary.handle_count,
        ancillary.credentials,
    };
}

os::core::Result<KernelPeerCredentials>
Channel::peer_credentials() const noexcept {
    if (!handle_.valid()) {
        return ipc_error(errors::invalid_channel);
    }

    ucred credentials {};
    socklen_t length = static_cast<socklen_t>(sizeof(credentials));
    if (::getsockopt(
            handle_.native(),
            SOL_SOCKET,
            SO_PEERCRED,
            &credentials,
            &length) != 0 || length != sizeof(credentials)) {
        return ipc_error(errors::transport_failure);
    }

    if (credentials.pid <= 0) {
        return ipc_error(errors::transport_failure);
    }

    return KernelPeerCredentials{
        .process_id = static_cast<std::int64_t>(credentials.pid),
        .user_id = static_cast<std::uint32_t>(credentials.uid),
        .group_id = static_cast<std::uint32_t>(credentials.gid),
    };
}

} // namespace os::ipc
