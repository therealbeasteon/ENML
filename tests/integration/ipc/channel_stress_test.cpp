#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/wire.hpp>

namespace {

[[nodiscard]] std::size_t count_open_fds() {
    DIR* directory = ::opendir("/proc/self/fd");
    assert(directory != nullptr);
    std::size_t count = 0U;
    while (auto* entry = ::readdir(directory)) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }
        ++count;
    }
    assert(::closedir(directory) == 0);
    assert(count > 0U);
    return count - 1U;
}

[[nodiscard]] os::ipc::WireHeaderV1 make_header(
    std::uint64_t request_id,
    std::uint32_t payload_size,
    std::uint16_t handle_count) {
    std::uint32_t flags = os::ipc::flag_value(os::ipc::WireFlag::request);
    if (handle_count != 0U) {
        flags |= os::ipc::flag_value(os::ipc::WireFlag::has_handles);
    }
    return os::ipc::WireHeaderV1{
        .flags = flags,
        .service_id = os::core::ServiceId{0xFFFF0901U},
        .operation_id = 1U,
        .request_id = os::core::RequestId{request_id},
        .payload_size = payload_size,
        .handle_count = handle_count,
        .payload_checksum = 0U,
    };
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    constexpr std::size_t iterations = 384U;
    constexpr std::size_t max_payload_iterations = 12U;

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        const auto baseline_fds = count_open_fds();
        std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};

        for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
            {
                auto receive_result = pair[1].receive(receive_buffer);
                if (!receive_result) std::_Exit(20);
                auto message = std::move(receive_result).value();

                const bool maximum_payload = iteration < max_payload_iterations;
                const auto expected_payload = maximum_payload
                    ? os::ipc::max_inline_payload_size
                    : static_cast<std::size_t>(32U);
                const auto expected_handles = (iteration % 8U == 0U)
                    ? os::ipc::max_handle_count
                    : static_cast<std::uint16_t>(0U);

                if (message.payload().size() != expected_payload ||
                    message.handle_count() != expected_handles ||
                    message.header().request_id.value() != iteration + 1U) {
                    std::_Exit(21);
                }

                for (const auto& handle : message.handles()) {
                    if (!handle.valid()) std::_Exit(22);
                    const int flags = ::fcntl(handle.native(), F_GETFD);
                    if (flags < 0 || (flags & FD_CLOEXEC) == 0) std::_Exit(23);
                }
            }

            if (count_open_fds() != baseline_fds) std::_Exit(24);
        }
        std::_Exit(0);
    }

    pair[1].close();

    std::array<os::core::NativeHandle, os::ipc::max_handle_count> handles{};
    for (auto& handle : handles) {
        const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        assert(fd >= 0);
        handle = os::core::NativeHandle{fd};
    }

    std::array<std::byte, os::ipc::max_inline_payload_size> maximum_payload{};
    std::array<std::byte, 32U> small_payload{};
    const auto baseline_fds = count_open_fds();

    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        const bool use_maximum_payload = iteration < max_payload_iterations;
        const auto payload = use_maximum_payload
            ? os::core::ByteSpan{maximum_payload}
            : os::core::ByteSpan{small_payload};
        const bool use_handles = iteration % 8U == 0U;
        const auto handle_span = use_handles
            ? std::span<const os::core::NativeHandle>{handles.data(), handles.size()}
            : std::span<const os::core::NativeHandle>{};

        const auto header = make_header(
            iteration + 1U,
            static_cast<std::uint32_t>(payload.size()),
            static_cast<std::uint16_t>(handle_span.size()));
        auto send_result = pair[0].send(header, payload, handle_span);
        assert(send_result);
    }

    assert(count_open_fds() == baseline_fds);
    pair[0].close();

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
