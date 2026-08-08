#include <array>
#include <cstddef>
#include <cstdint>

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

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    auto request = os::app::receive_bootstrap_request(channel.value(), receive_buffer);
    if (!request) return 11;

    if (request.value().record.package_generation !=
        static_cast<std::uint64_t>(EXPECTED_GENERATION)) {
        return 12;
    }
    if (request.value().record.instance.value() == 0U ||
        !os::core::valid_peer_identity(request.value().record.identity)) {
        return 13;
    }

    auto ready = os::app::send_ready(
        channel.value(),
        request.value().request_header,
        request.value().record);
    if (!ready) return 14;

    // Stay bound to this immutable launch generation until App Manager ends
    // the instance. The test intentionally has no path/package selection API.
    for (;;) {
        (void)::pause();
    }
}
