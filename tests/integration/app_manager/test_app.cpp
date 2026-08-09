#include <array>
#include <cstddef>
#include <cstdint>

#include <unistd.h>

#include <os/app/bootstrap.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/span.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/storage/path.hpp>
#include <os/storage/service.hpp>

#ifndef EXPECTED_GENERATION
#error EXPECTED_GENERATION must be defined
#endif

int main() {
    auto bootstrap_channel = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::app::application_bootstrap_fd});
    if (!bootstrap_channel) return 10;

    auto storage_channel = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::app::application_storage_service_fd});
    if (!storage_channel) return 11;

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    auto request = os::app::receive_bootstrap_request(
        bootstrap_channel.value(), receive_buffer);
    if (!request) return 12;

    if (request.value().record.package_generation !=
        static_cast<std::uint64_t>(EXPECTED_GENERATION)) {
        return 13;
    }
    if (request.value().record.instance.value() == 0U ||
        !os::core::valid_peer_identity(request.value().record.identity)) {
        return 14;
    }

    // The application has no private-data directory descriptor. Its first
    // storage access goes through the identity-authenticated Storage Service
    // endpoint and returns a typed root object capability.
    os::ipc::ClientConnection storage_connection{storage_channel.value()};
    os::storage::StorageClient storage{storage_connection};
    auto root = storage.open_private_root(receive_buffer);
    if (!root) return 15;

    auto probe_path = os::storage::RelativePath::parse("storage-probe.bin");
    if (!probe_path) return 16;
    const std::array<std::byte, 1U> probe{
        static_cast<std::byte>(static_cast<std::uint8_t>(EXPECTED_GENERATION)),
    };
    auto replaced = root.value().atomic_replace(probe_path.value(), probe, receive_buffer);
    if (!replaced) return 17;

    auto ready = os::app::send_ready(
        bootstrap_channel.value(),
        request.value().request_header,
        request.value().record);
    if (!ready) return 18;

    for (;;) {
        (void)::pause();
    }
}
