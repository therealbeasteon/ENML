#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>
#include <os/ipc/wire.hpp>

#include "echo_codec.generated.hpp"
#include "echo_server.generated.hpp"
#include "test_identity.hpp"

namespace generated = os::test::echo;

namespace {

constexpr os::core::PeerIdentity test_identity{
    .principal = os::core::PrincipalId{0xAAULL, 0xBBULL},
    .user = os::core::UserId{3U},
    .process = os::core::ProcessId{77U},
};

class EchoImplementation final : public generated::EchoServer {
public:
    os::core::Result<generated::PingResponse>
    ping(const os::ipc::RequestContext&, const generated::PingRequest& request) noexcept override {
        return generated::PingResponse{.text = request.text};
    }

    os::core::Result<generated::IdentityResponse>
    identify(const os::ipc::RequestContext& context, const generated::Empty&) noexcept override {
        return generated::IdentityResponse{
            .process_id = context.peer.process.value(),
            .principal_high = context.peer.principal.high,
            .principal_low = context.peer.principal.low,
            .user_id = context.peer.user.value(),
        };
    }

    os::core::Result<generated::IdentityResponse>
    identify_claim(const os::ipc::RequestContext& context, const generated::IdentityClaimRequest&) noexcept override {
        return identify(context, generated::Empty{});
    }
};

[[noreturn]] void run_server(os::ipc::Channel channel, pid_t client_pid) {
    TestIdentityResolver resolver{
        static_cast<std::int64_t>(client_pid),
        static_cast<std::uint32_t>(::getuid()),
        static_cast<std::uint32_t>(::getgid()),
        test_identity};
    EchoImplementation implementation;
    generated::EchoDispatcher dispatcher{implementation, channel, resolver};
    std::array<std::byte, os::ipc::max_wire_packet_size> buffer{};
    for (int index = 0; index < 3; ++index) {
        auto result = dispatcher.dispatch_one(buffer);
        if (!result) std::_Exit(10 + index);
    }
    std::_Exit(0);
}

os::ipc::WireHeaderV1 request_header(
    std::uint32_t operation,
    std::uint64_t request,
    std::uint32_t payload_size) {
    return os::ipc::WireHeaderV1{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = generated::EchoDispatcher::service_id,
        .operation_id = operation,
        .request_id = os::core::RequestId{request},
        .payload_size = payload_size,
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
}

void expect_error(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan buffer,
    os::core::ErrorDomain domain,
    std::uint32_t code) {
    auto response_result = channel.receive(buffer);
    assert(response_result);
    auto response = std::move(response_result).value();
    assert((response.header().flags & os::ipc::flag_value(os::ipc::WireFlag::error)) != 0U);
    os::core::Error decoded{};
    auto decode_result = os::ipc::decode_rpc_error(response.payload(), decoded);
    assert(decode_result);
    assert(decoded.domain == domain);
    assert(decoded.code == code);
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();
    const pid_t parent_pid = ::getpid();
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        channels[0].close();
        run_server(std::move(channels[1]), parent_pid);
    }

    channels[1].close();
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};

    const auto unknown = request_header(999U, 1U, 0U);
    assert(channels[0].send(unknown, {}));
    expect_error(channels[0], receive_buffer, os::core::ErrorDomain::service,
                 os::core::errors::service::unknown_operation);

    std::array<std::byte, 1> malformed{std::byte{0xFF}};
    const auto malformed_header = request_header(1U, 2U, 1U);
    assert(channels[0].send(malformed_header, malformed));
    expect_error(channels[0], receive_buffer, os::core::ErrorDomain::ipc,
                 os::ipc::errors::malformed_message);

    std::array<std::byte, 64> payload_storage{};
    os::ipc::Encoder encoder{payload_storage};
    assert(generated::encode_ping_request(encoder, generated::PingRequest{.text = "still-alive"}));
    const auto valid_header = request_header(1U, 3U, static_cast<std::uint32_t>(encoder.written().size()));
    assert(channels[0].send(valid_header, encoder.written()));
    auto response_result = channels[0].receive(receive_buffer);
    assert(response_result);
    auto response = std::move(response_result).value();
    assert((response.header().flags & os::ipc::flag_value(os::ipc::WireFlag::error)) == 0U);
    os::ipc::Decoder decoder{response.payload()};
    auto ping = generated::decode_ping_response(decoder);
    assert(ping);
    assert(ping.value().text == "still-alive");

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
