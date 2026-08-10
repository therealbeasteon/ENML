#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/accessibility/transport.hpp>
#include <os/app/accessibility_control.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/wire.hpp>

namespace {

constexpr os::core::PrincipalId application_principal{
    0x4150504C49434154ULL,
    0x494F4E0000000001ULL,
};
constexpr os::core::PrincipalId ordinary_principal{
    0x4F5244494E415259ULL,
    0x4150500000000001ULL,
};
constexpr os::core::PeerIdentity application_peer{
    .principal = application_principal,
    .user = os::core::UserId{77U},
    .process = os::core::ProcessId{501U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    void expect(pid_t native_pid, os::core::PeerIdentity peer) noexcept {
        native_pid_ = native_pid;
        peer_ = peer;
    }

    [[nodiscard]] os::core::Result<os::core::PeerIdentity> resolve(
        os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (native_pid_ <= 0 ||
            credentials.process_id != static_cast<std::int64_t>(native_pid_) ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid()) ||
            !os::core::valid_peer_identity(peer_)) {
            return os::core::make_error(
                os::core::ErrorDomain::security,
                os::core::errors::security::credential_mismatch);
        }
        return peer_;
    }

private:
    pid_t native_pid_ {-1};
    os::core::PeerIdentity peer_ {};
};

struct FakeBroker final {
    os::core::PeerIdentity expected_target {};
    os::app::BrokeredAccessibilityEndpoint endpoint {};
    std::uint32_t calls {0U};
};

[[nodiscard]] os::core::Result<os::app::BrokeredAccessibilityEndpoint> claim_endpoint(
    void* context,
    os::core::PeerIdentity caller,
    os::core::PeerIdentity target) noexcept {
    auto* broker = static_cast<FakeBroker*>(context);
    if (broker == nullptr ||
        caller.principal != os::accessibility::accessibility_service_principal) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::app::manager_errors::accessibility_authority_denied);
    }
    ++broker->calls;
    if (target != broker->expected_target) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::app::manager_errors::accessibility_target_not_found);
    }
    if (!broker->endpoint.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::app::manager_errors::accessibility_endpoint_unavailable);
    }
    return std::move(broker->endpoint);
}

[[nodiscard]] os::core::PeerIdentity service_peer(
    os::core::PrincipalId principal,
    std::uint64_t process) noexcept {
    return os::core::PeerIdentity{
        .principal = principal,
        .user = os::core::UserId{0U},
        .process = os::core::ProcessId{process},
    };
}

} // namespace

int main() {
    TestIdentityResolver resolver{};

    auto accessibility_pair_result = os::ipc::Channel::create_local_pair();
    assert(accessibility_pair_result);
    auto accessibility_pair = std::move(accessibility_pair_result).value();

    FakeBroker broker{};
    broker.expected_target = application_peer;
    broker.endpoint.session_id = 0xA11CE551U;
    broker.endpoint.application = application_peer;
    broker.endpoint.channel = std::move(accessibility_pair[1]);
    assert(broker.endpoint.valid());

    const os::app::AccessibilityEndpointBrokerBackend backend{
        .context = &broker,
        .claim = claim_endpoint,
    };
    os::app::AccessibilityBrokerControlServer server{backend, resolver};
    assert(server.valid());

    auto control_pair_result = os::ipc::Channel::create_local_pair();
    assert(control_pair_result);
    auto control_pair = std::move(control_pair_result).value();

    const pid_t trusted_child = ::fork();
    assert(trusted_child >= 0);
    if (trusted_child == 0) {
        control_pair[0].close();
        accessibility_pair[0].close();
        broker.endpoint.channel.close();

        os::app::AccessibilityBrokerControlClient client{control_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto claimed = client.claim(application_peer, scratch);
        if (!claimed || !claimed.value().valid()) ::_exit(20);
        if (claimed.value().session_id != 0xA11CE551U ||
            claimed.value().application != application_peer) {
            ::_exit(21);
        }

        const std::array<std::byte, 1U> marker{std::byte{0xA5}};
        const os::ipc::WireHeaderV1 header{
            .flags = os::ipc::flag_value(os::ipc::WireFlag::event),
            .service_id = os::core::ServiceId{0x0000F0FEU},
            .operation_id = 1U,
            .request_id = os::core::RequestId{1U},
            .payload_size = 1U,
            .handle_count = 0U,
            .payload_checksum = 0U,
        };
        auto sent = claimed.value().channel.send(header, marker);
        if (!sent) ::_exit(22);
        ::_exit(0);
    }

    control_pair[1].close();
    resolver.expect(
        trusted_child,
        service_peer(os::accessibility::accessibility_service_principal, 9001U));
    std::array<std::byte, os::ipc::max_wire_packet_size> server_scratch{};
    auto handled = server.dispatch_once(control_pair[0], server_scratch);
    assert(handled);
    assert(broker.calls == 1U);
    assert(!broker.endpoint.valid());

    std::array<std::byte, os::ipc::max_wire_packet_size> app_scratch{};
    auto delivered = accessibility_pair[0].receive(app_scratch);
    assert(delivered);
    assert(delivered.value().payload().size() == 1U);
    assert(delivered.value().payload()[0] == std::byte{0xA5});

    int trusted_status = 0;
    assert(::waitpid(trusted_child, &trusted_status, 0) == trusted_child);
    assert(WIFEXITED(trusted_status));
    assert(WEXITSTATUS(trusted_status) == 0);

    // A different kernel sender may know the private service id and exact app
    // identity, but its resolved principal is denied before target decoding and
    // the claim backend is never entered.
    auto denied_pair_result = os::ipc::Channel::create_local_pair();
    assert(denied_pair_result);
    auto denied_pair = std::move(denied_pair_result).value();

    const pid_t ordinary_child = ::fork();
    assert(ordinary_child >= 0);
    if (ordinary_child == 0) {
        denied_pair[0].close();
        os::app::AccessibilityBrokerControlClient client{denied_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto denied = client.claim(application_peer, scratch);
        if (denied ||
            denied.error().domain != os::core::ErrorDomain::service ||
            denied.error().code != os::app::manager_errors::accessibility_authority_denied) {
            ::_exit(30);
        }
        ::_exit(0);
    }

    denied_pair[1].close();
    resolver.expect(ordinary_child, service_peer(ordinary_principal, 9002U));
    auto denied_handled = server.dispatch_once(denied_pair[0], server_scratch);
    assert(denied_handled);
    assert(broker.calls == 1U);

    int ordinary_status = 0;
    assert(::waitpid(ordinary_child, &ordinary_status, 0) == ordinary_child);
    assert(WIFEXITED(ordinary_status));
    assert(WEXITSTATUS(ordinary_status) == 0);

    return 0;
}
