#include <array>
#include <cstddef>
#include <utility>

#include <sys/stat.h>

#include <os/app/bootstrap.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/storage/path.hpp>
#include <os/storage/service.hpp>

int main() {
    auto bootstrap_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::app::application_bootstrap_fd});
    if (!bootstrap_result) return 10;
    auto bootstrap_channel = std::move(bootstrap_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap = os::app::receive_bootstrap_request(
        bootstrap_channel,
        bootstrap_buffer);
    if (!bootstrap) return 11;

    struct stat resource_info {};
    if (::fstat(os::app::application_storage_service_fd, &resource_info) != 0 ||
        !S_ISSOCK(resource_info.st_mode)) {
        return 12;
    }

    auto storage_channel_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::app::application_storage_service_fd});
    if (!storage_channel_result) return 13;
    auto storage_channel = std::move(storage_channel_result).value();
    os::ipc::ClientConnection storage_connection{storage_channel};
    os::storage::StorageClient storage{storage_connection};

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto root_result = storage.open_private_root(scratch);
    if (!root_result) return 14;
    auto root = std::move(root_result).value();

    auto path = os::storage::RelativePath::parse("brokered.txt");
    if (!path) return 15;
    constexpr std::array<std::byte, 8U> contents{
        std::byte{'b'}, std::byte{'r'}, std::byte{'o'}, std::byte{'k'},
        std::byte{'e'}, std::byte{'r'}, std::byte{'e'}, std::byte{'d'},
    };
    auto replaced = root.atomic_replace(path.value(), contents, scratch);
    if (!replaced) return 16;

    auto ready = os::app::send_ready(
        bootstrap_channel,
        bootstrap.value().request_header,
        bootstrap.value().record);
    if (!ready) return 17;
    return 0;
}
