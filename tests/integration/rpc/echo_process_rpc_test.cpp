#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>

#include "echo_client.generated.hpp"
#include "echo_server.generated.hpp"
#include "test_identity.hpp"

namespace generated = os::test::echo;

namespace {

constexpr os::core::PeerIdentity test_identity{
    .principal = os::core::PrincipalId{0x1111111111111111ULL, 0x2222222222222222ULL},
    .user = os::core::UserId{7U},
    .process = os::core::ProcessId{42U},
};

[[nodiscard]] generated::IdentityResponse identity_response(const os::ipc::RequestContext& context) {
    return generated::IdentityResponse{
        .process_id = context.peer.process.value(),
        .principal_high = context.peer.principal.high,
        .principal_low = context.peer.principal.low,
        .user_id = context.peer.user.value(),
    };
}

class EchoImplementation final : public generated::EchoServer {
public:
    os::core::Result<generated::PingResponse>
    ping(const os::ipc::RequestContext&, const generated::PingRequest& request) noexcept override {
        return generated::PingResponse{.text = request.text};
    }

    os::core::Result<generated::IdentityResponse>
    identify(const os::ipc::RequestContext& context, const generated::Empty&) noexcept override {
        return identity_response(context);
    }

    os::core::Result<generated::IdentityResponse>
    identify_claim(const os::ipc::RequestContext& context, const generated::IdentityClaimRequest&) noexcept override {
        return identity_response(context);
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
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};

    for (int index = 0; index < 4; ++index) {
        const auto result = dispatcher.dispatch_one(receive_buffer);
        if (!result) std::_Exit(10 + index);
    }
    std::_Exit(0);
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
    os::ipc::ClientConnection connection{channels[0]};
    generated::EchoClient client{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> response_buffer{};

    auto ping_result = client.ping(generated::PingRequest{.text = "hello"}, response_buffer);
    assert(ping_result);
    assert(ping_result.value().text == "hello");

    auto identify_result = client.identify(generated::Empty{}, response_buffer);
    assert(identify_result);
    assert(identify_result.value().process_id == test_identity.process.value());
    assert(identify_result.value().principal_high == test_identity.principal.high);
    assert(identify_result.value().principal_low == test_identity.principal.low);
    assert(identify_result.value().user_id == test_identity.user.value());

    auto claim_result = client.identify_claim(
        generated::IdentityClaimRequest{
            .claimed_process_id = 999999U,
            .claimed_principal_high = 0xFFFFFFFFFFFFFFFFULL,
            .claimed_principal_low = 0xFFFFFFFFFFFFFFFFULL,
            .claimed_user_id = 999U,
        }, response_buffer);
    assert(claim_result);
    assert(claim_result.value().process_id == test_identity.process.value());
    assert(claim_result.value().principal_high == test_identity.principal.high);
    assert(claim_result.value().principal_low == test_identity.principal.low);
    assert(claim_result.value().user_id == test_identity.user.value());

    const std::string maximum_text(256U, 'x');
    auto max_result = client.ping(generated::PingRequest{.text = maximum_text}, response_buffer);
    assert(max_result);
    assert(max_result.value().text == maximum_text);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    auto dead_result = client.ping(generated::PingRequest{.text = "after-death"}, response_buffer);
    assert(!dead_result);
    assert(dead_result.error().domain == os::core::ErrorDomain::ipc);
    assert(dead_result.error().code == os::ipc::errors::peer_died);
    return 0;
}
