#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <os/app/bootstrap.hpp>
#include <os/app/service_session.hpp>
#include <os/core/error.hpp>
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

[[nodiscard]] bool is_peer_died(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::ipc &&
        error.code == os::ipc::errors::peer_died;
}

void short_delay() noexcept {
    timespec delay{.tv_sec = 0, .tv_nsec = 5'000'000L};
    while (::nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
}

[[nodiscard]] bool equal_plaintext(
    os::core::ByteSpan expected,
    const std::array<std::byte, os::keys::max_key_plaintext_bytes>& actual,
    std::size_t actual_size) noexcept {
    return actual_size == expected.size() &&
        std::equal(
            actual.begin(),
            actual.begin() + static_cast<std::ptrdiff_t>(actual_size),
            expected.begin());
}

} // namespace

int main() {
    auto bootstrap_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::app::application_bootstrap_fd});
    if (!bootstrap_result) return 10;
    auto bootstrap_channel = std::move(bootstrap_result).value();

    errno = 0;
    if (::fcntl(os::app::application_storage_service_fd, F_GETFD) != -1 || errno != EBADF) {
        return 11;
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto request_result = os::app::receive_bootstrap_request_v2(bootstrap_channel, scratch);
    if (!request_result) return 12;
    auto request = std::move(request_result).value();
    if (request.service_count != 2U ||
        request.services[0] != os::storage::storage_service_id ||
        request.services[1] != os::keys::key_service_id ||
        !os::core::valid_peer_identity(request.record.identity)) {
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

    os::ipc::ClientConnection storage_connection{storage_channel};
    os::storage::StorageClient storage{storage_connection};
    auto root_result = storage.open_private_root(scratch);
    if (!root_result) return 16;
    auto storage_root = std::move(root_result).value();

    auto initial_path = os::storage::RelativePath::parse("m2-10-initial.bin");
    auto session_ready_path = os::storage::RelativePath::parse("m2-10-session-ready.bin");
    auto key_reacquired_path = os::storage::RelativePath::parse("m2-10-key-reacquired.bin");
    auto heartbeat_path = os::storage::RelativePath::parse("m2-10-storage-heartbeat.bin");
    auto storage_reacquired_path = os::storage::RelativePath::parse("m2-10-storage-reacquired.bin");
    if (!initial_path || !session_ready_path || !key_reacquired_path ||
        !heartbeat_path || !storage_reacquired_path) {
        return 17;
    }

    const std::array<std::byte, 4U> initial_marker{
        std::byte{0x4D}, std::byte{0x32}, std::byte{0x31}, std::byte{0x30},
    };
    if (!storage_root.atomic_replace(initial_path.value(), initial_marker, scratch)) return 18;

    os::ipc::ClientConnection key_connection{key_channel};
    os::keys::KeyClient keys{key_connection};
    auto created = keys.create_application_data_key(scratch);
    if (!created) return 19;
    auto key = std::move(created).value();
    const auto key_id = key.descriptor().id;

    constexpr std::string_view plaintext = "m2.10-runtime-service-session";
    constexpr std::string_view aad = "generation-bound-reacquisition";
    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope{};
    std::array<std::byte, os::keys::max_key_plaintext_bytes> decrypted{};
    auto encrypted = key.encrypt(as_bytes(plaintext), as_bytes(aad), envelope, scratch);
    if (!encrypted) return 20;
    const std::size_t envelope_size = encrypted.value();

    const std::array services{
        os::storage::storage_service_id,
        os::keys::key_service_id,
    };
    auto ready = os::app::send_ready_v2(
        bootstrap_channel,
        request.request_header,
        request.record,
        std::span<const os::core::ServiceId>{services});
    if (!ready) return 21;

    os::app::PlatformServiceSession runtime{bootstrap_channel};

    // The bootstrap-v2 service set is a strict allow-list. The runtime session
    // cannot be used as a general service bus or to expand application policy.
    constexpr os::core::ServiceId unauthorized_service{0x0000F0FFU};
    auto denied = runtime.acquire(unauthorized_service, 0U, scratch);
    if (denied || denied.error().domain != os::core::ErrorDomain::service ||
        denied.error().code != os::core::errors::service::access_denied) {
        return 22;
    }

    // Observe the current generations through the trusted runtime session. The
    // transferred duplicate endpoints are intentionally discarded; the
    // original bootstrap capabilities below are used to prove staleness.
    auto key_observed = runtime.acquire(os::keys::key_service_id, 0U, scratch);
    if (!key_observed) return 23;
    const std::uint64_t old_key_generation = key_observed.value().generation;
    key_observed.value().channel.close();

    auto storage_observed = runtime.acquire(os::storage::storage_service_id, 0U, scratch);
    if (!storage_observed) return 24;
    const std::uint64_t old_storage_generation = storage_observed.value().generation;
    storage_observed.value().channel.close();

    const std::array<std::byte, 1U> ready_marker{std::byte{0xA1}};
    if (!storage_root.atomic_replace(session_ready_path.value(), ready_marker, scratch)) return 25;

    // Wait for system.keys to die. The original KeyObject capability must fail
    // permanently; it is never rebound behind the application's back.
    bool key_died = false;
    for (std::size_t attempt = 0U; attempt < 1000U; ++attempt) {
        auto opened = key.decrypt(
            {envelope.data(), envelope_size},
            as_bytes(aad),
            decrypted,
            scratch);
        if (!opened) {
            if (!is_peer_died(opened.error())) return 26;
            key_died = true;
            break;
        }
        if (!equal_plaintext(as_bytes(plaintext), decrypted, opened.value())) return 27;
        short_delay();
    }
    if (!key_died) return 28;

    auto fresh_key_endpoint = runtime.acquire(
        os::keys::key_service_id,
        old_key_generation,
        scratch);
    if (!fresh_key_endpoint ||
        fresh_key_endpoint.value().generation == old_key_generation) {
        return 29;
    }
    auto fresh_key_channel = std::move(fresh_key_endpoint).value().channel;
    os::ipc::ClientConnection fresh_key_connection{fresh_key_channel};
    os::keys::KeyClient fresh_keys{fresh_key_connection};
    auto reopened = fresh_keys.open(key_id, scratch);
    if (!reopened) return 30;
    auto fresh_key = std::move(reopened).value();
    auto recovered = fresh_key.decrypt(
        {envelope.data(), envelope_size},
        as_bytes(aad),
        decrypted,
        scratch);
    if (!recovered ||
        !equal_plaintext(as_bytes(plaintext), decrypted, recovered.value())) {
        return 31;
    }

    const std::array<std::byte, 1U> key_marker{std::byte{0xB2}};
    if (!storage_root.atomic_replace(key_reacquired_path.value(), key_marker, scratch)) return 32;

    // Now wait for system.storage to die. Repeated bounded writes are merely a
    // liveness probe on the old root object; only peer_died advances the test.
    const std::array<std::byte, 1U> heartbeat{std::byte{0xC3}};
    bool storage_died = false;
    for (std::size_t attempt = 0U; attempt < 1000U; ++attempt) {
        auto written = storage_root.atomic_replace(heartbeat_path.value(), heartbeat, scratch);
        if (!written) {
            if (!is_peer_died(written.error())) return 33;
            storage_died = true;
            break;
        }
        short_delay();
    }
    if (!storage_died) return 34;

    auto fresh_storage_endpoint = runtime.acquire(
        os::storage::storage_service_id,
        old_storage_generation,
        scratch);
    if (!fresh_storage_endpoint ||
        fresh_storage_endpoint.value().generation == old_storage_generation) {
        return 35;
    }
    auto fresh_storage_channel = std::move(fresh_storage_endpoint).value().channel;
    os::ipc::ClientConnection fresh_storage_connection{fresh_storage_channel};
    os::storage::StorageClient fresh_storage{fresh_storage_connection};
    auto fresh_root_result = fresh_storage.open_private_root(scratch);
    if (!fresh_root_result) return 36;
    auto fresh_root = std::move(fresh_root_result).value();
    const std::array<std::byte, 1U> storage_marker{std::byte{0xD4}};
    if (!fresh_root.atomic_replace(
            storage_reacquired_path.value(),
            storage_marker,
            scratch)) {
        return 37;
    }

    for (;;) (void)::pause();
}
