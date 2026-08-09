#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <os/app/bootstrap.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/service.hpp>
#include <os/storage/path.hpp>
#include <os/storage/service.hpp>

namespace {

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

} // namespace

int main() {
    auto bootstrap_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::app::application_bootstrap_fd});
    if (!bootstrap_result) return 10;
    auto bootstrap_channel = std::move(bootstrap_result).value();

    // V2 applications inherit no service-specific fixed descriptor. fd 5 is
    // deliberately closed before exec; recvmsg may later choose that numeric fd
    // for one transferred endpoint, but the number has no semantic meaning.
    errno = 0;
    if (::fcntl(os::app::application_storage_service_fd, F_GETFD) != -1 || errno != EBADF) {
        return 11;
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto request_result = os::app::receive_bootstrap_request_v2(
        bootstrap_channel,
        scratch);
    if (!request_result) return 12;
    auto request = std::move(request_result).value();
    if (request.service_count != 2U ||
        request.services[0] != os::storage::storage_service_id ||
        request.services[1] != os::keys::key_service_id ||
        request.record.instance.value() == 0U ||
        !os::core::valid_peer_identity(request.record.identity) ||
        request.record.package_generation == 0U) {
        return 13;
    }

    auto storage_handle = request.take_service_endpoint(os::storage::storage_service_id);
    auto key_handle = request.take_service_endpoint(os::keys::key_service_id);
    if (!storage_handle || !key_handle) return 14;

    auto storage_channel_result = os::ipc::Channel::adopt(std::move(storage_handle).value());
    auto key_channel_result = os::ipc::Channel::adopt(std::move(key_handle).value());
    if (!storage_channel_result || !key_channel_result) return 15;
    auto storage_channel = std::move(storage_channel_result).value();
    auto key_channel = std::move(key_channel_result).value();

    // Both services resolve this process from the same broker-published
    // PeerIdentity. The app never supplied PrincipalId/UserId/ProcessId to
    // either public request.
    os::ipc::ClientConnection storage_connection{storage_channel};
    os::storage::StorageClient storage{storage_connection};
    auto root = storage.open_private_root(scratch);
    if (!root) return 16;
    auto probe_path = os::storage::RelativePath::parse("broker-v2-storage.bin");
    if (!probe_path) return 17;
    const std::array<std::byte, 4U> storage_probe{
        std::byte{0x42}, std::byte{0x52}, std::byte{0x4B}, std::byte{0x32},
    };
    auto replaced = root.value().atomic_replace(probe_path.value(), storage_probe, scratch);
    if (!replaced) return 18;

    os::ipc::ClientConnection key_connection{key_channel};
    os::keys::KeyClient keys{key_connection};
    auto created = keys.create_application_data_key(scratch);
    if (!created) return 19;
    auto key = std::move(created).value();

    constexpr std::string_view plaintext = "brokered-storage-and-keys";
    constexpr std::string_view aad = "m2.9-bootstrap-v2";
    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope{};
    std::array<std::byte, os::keys::max_key_plaintext_bytes> decrypted{};
    auto encrypted = key.encrypt(as_bytes(plaintext), as_bytes(aad), envelope, scratch);
    if (!encrypted) return 20;
    auto opened = key.decrypt(
        {envelope.data(), encrypted.value()},
        as_bytes(aad),
        decrypted,
        scratch);
    if (!opened || opened.value() != plaintext.size()) return 21;
    if (!std::equal(
            decrypted.begin(),
            decrypted.begin() + static_cast<std::ptrdiff_t>(opened.value()),
            as_bytes(plaintext).begin())) {
        return 22;
    }

    const std::array services{
        os::storage::storage_service_id,
        os::keys::key_service_id,
    };
    auto ready = os::app::send_ready_v2(
        bootstrap_channel,
        request.request_header,
        request.record,
        std::span<const os::core::ServiceId>{services});
    if (!ready) return 23;

    for (;;) (void)::pause();
}
