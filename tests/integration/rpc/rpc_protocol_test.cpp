#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/ipc/wire.hpp>

namespace {

[[noreturn]] void wrong_request_id_server(os::ipc::Channel channel) {
    std::array<std::byte, os::ipc::max_wire_packet_size> buffer{};
    auto request_result = channel.receive(buffer);
    if (!request_result) std::_Exit(10);
    auto request = std::move(request_result).value();

    const auto bad_id = os::core::RequestId{request.header().request_id.value() + 1U};
    const os::ipc::WireHeaderV1 response{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::response),
        .service_id = request.header().service_id,
        .operation_id = request.header().operation_id,
        .request_id = bad_id,
        .payload_size = 0U,
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    if (!channel.send(response, {})) std::_Exit(11);
    std::_Exit(0);
}

[[noreturn]] void distinct_ids_server(os::ipc::Channel channel) {
    std::array<std::byte, os::ipc::max_wire_packet_size> buffer{};
    std::uint64_t first = 0U;
    for (int index = 0; index < 2; ++index) {
        auto request_result = channel.receive(buffer);
        if (!request_result) std::_Exit(20 + index);
        auto request = std::move(request_result).value();
        if (request.header().request_id.value() == 0U) std::_Exit(23);
        if (index == 0) {
            first = request.header().request_id.value();
        } else if (request.header().request_id.value() == first) {
            std::_Exit(24);
        }
        if (!os::ipc::send_rpc_response(channel, request.header(), {})) std::_Exit(25);
    }
    std::_Exit(0);
}

} // namespace

int main() {
    {
        auto pair_result = os::ipc::Channel::create_local_pair();
        assert(pair_result);
        auto channels = std::move(pair_result).value();
        const pid_t child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            channels[0].close();
            wrong_request_id_server(std::move(channels[1]));
        }
        channels[1].close();
        os::ipc::ClientConnection connection{channels[0]};
        std::array<std::byte, os::ipc::max_wire_packet_size> buffer{};
        auto result = connection.call(os::core::ServiceId{42U}, 7U, {}, buffer);
        assert(!result);
        assert(result.error().domain == os::core::ErrorDomain::ipc);
        assert(result.error().code == os::ipc::errors::protocol_violation);
        int status = 0;
        assert(::waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    {
        auto pair_result = os::ipc::Channel::create_local_pair();
        assert(pair_result);
        auto channels = std::move(pair_result).value();
        const pid_t child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            channels[0].close();
            distinct_ids_server(std::move(channels[1]));
        }
        channels[1].close();
        os::ipc::ClientConnection connection{channels[0]};
        std::array<std::byte, os::ipc::max_wire_packet_size> buffer{};
        assert(connection.call(os::core::ServiceId{42U}, 7U, {}, buffer));
        assert(connection.call(os::core::ServiceId{42U}, 7U, {}, buffer));
        int status = 0;
        assert(::waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    return 0;
}
