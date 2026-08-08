#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>

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

class Echo final : public generated::EchoServer {
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
    identify_claim(
        const os::ipc::RequestContext& context,
        const generated::IdentityClaimRequest&) noexcept override {
        return identify(context, generated::Empty{});
    }
};

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        Echo implementation;
        const os::core::PeerIdentity identity{
            .principal = os::core::PrincipalId{0x5445535400000000ULL, 0x000000000000B001ULL},
            .user = os::core::UserId{1U},
            .process = os::core::ProcessId{1U},
        };
        TestIdentityResolver resolver{
            static_cast<std::int64_t>(::getppid()),
            static_cast<std::uint32_t>(::getuid()),
            static_cast<std::uint32_t>(::getgid()),
            identity};
        generated::EchoDispatcher dispatcher{implementation, pair[1], resolver};
        std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
        for (;;) {
            auto result = dispatcher.dispatch_one(receive_buffer);
            if (!result) {
                if (result.error().domain == os::core::ErrorDomain::ipc &&
                    result.error().code == os::ipc::errors::peer_died) {
                    std::_Exit(0);
                }
                std::_Exit(20);
            }
        }
    }

    pair[1].close();
    os::ipc::ClientConnection connection{pair[0]};
    generated::EchoClient client{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> response{};

    constexpr std::size_t calls = 2000U;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0U; index < calls; ++index) {
        auto result = client.ping(generated::PingRequest{.text = "baseline"}, response);
        assert(result);
        assert(result.value().text == "baseline");
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    assert(microseconds >= 0);

    std::cout << "EMNL_M0_RPC_BASELINE calls=" << calls
              << " total_us=" << microseconds
              << " avg_us=" << (static_cast<double>(microseconds) / static_cast<double>(calls))
              << '\n';

    pair[0].close();
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
