#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>

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
std::size_t allocation_count = 0;

constexpr os::core::PeerIdentity test_identity{
    .principal = os::core::PrincipalId{1U, 2U},
    .user = os::core::UserId{1U},
    .process = os::core::ProcessId{12U},
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
    const auto result = dispatcher.dispatch_one(buffer);
    std::_Exit(result ? 0 : 10);
}
} // namespace

void* operator new(std::size_t size) {
    ++allocation_count;
    if (void* memory = std::malloc(size)) return memory;
    std::abort();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

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

    const auto before = allocation_count;
    auto result = client.ping(generated::PingRequest{.text = "allocation-free"}, response_buffer);
    const auto after = allocation_count;
    assert(result);
    assert(result.value().text == "allocation-free");
    assert(after == before);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
