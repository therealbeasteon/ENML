#include <array>
#include <cstddef>
#include <cstdint>

#include <sys/stat.h>
#include <unistd.h>

#include <os/app/bootstrap.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>

#ifndef EXPECTED_GENERATION
#error EXPECTED_GENERATION must be defined
#endif

int main() {
    auto channel = os::ipc::Channel::adopt(os::core::NativeHandle{os::app::application_bootstrap_fd});
    if (!channel) return 10;

    struct stat private_data_info {};
    if (::fstat(os::app::application_private_data_fd, &private_data_info) != 0 ||
        !S_ISDIR(private_data_info.st_mode)) {
        return 11;
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    auto request = os::app::receive_bootstrap_request(channel.value(), receive_buffer);
    if (!request) return 12;

    if (request.value().record.package_generation !=
        static_cast<std::uint64_t>(EXPECTED_GENERATION)) {
        return 13;
    }
    if (request.value().record.instance.value() == 0U ||
        !os::core::valid_peer_identity(request.value().record.identity)) {
        return 14;
    }

    auto ready = os::app::send_ready(
        channel.value(),
        request.value().request_header,
        request.value().record);
    if (!ready) return 15;

    for (;;) {
        (void)::pause();
    }
}
