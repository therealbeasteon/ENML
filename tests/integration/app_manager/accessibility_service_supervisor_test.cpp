#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/accessibility/service.hpp>
#include <os/accessibility/transport.hpp>
#include <os/app/accessibility_control.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/supervisor/supervisor.hpp>
#include <os/ui/accessibility.hpp>

namespace {

constexpr os::core::PrincipalId application_principal{
    0x4153434150500001ULL,
    0x0000000000000001ULL,
};
constexpr os::core::PeerIdentity application_peer{
    .principal = application_principal,
    .user = os::core::UserId{88U},
    .process = os::core::ProcessId{8801U},
};
constexpr std::uint64_t session_value = 0xA11CE570U;

[[nodiscard]] os::ui::LogicalRect rect(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    return os::ui::LogicalRect{
        .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(x)),
        .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(y)),
        .width_q6 = os::ui::logical_from_dp(width),
        .height_q6 = os::ui::logical_from_dp(height),
    };
}

[[nodiscard]] os::ui::SemanticText text(std::string_view value) {
    auto result = os::ui::make_semantic_text(value);
    assert(result);
    return result.value();
}

class FixedResolver final : public os::ipc::PeerIdentityResolver {
public:
    FixedResolver(pid_t native_pid, os::core::PeerIdentity peer) noexcept
        : native_pid_(native_pid), peer_(peer) {}

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
    os::core::PeerIdentity target {};
    os::app::BrokeredAccessibilityEndpoint endpoint {};
};

[[nodiscard]] os::core::Result<os::app::BrokeredAccessibilityEndpoint> claim_endpoint(
    void* context,
    os::core::PeerIdentity caller,
    os::core::PeerIdentity target) noexcept {
    auto* broker = static_cast<FakeBroker*>(context);
    if (broker == nullptr ||
        caller.principal != os::accessibility::accessibility_service_principal ||
        target != broker->target || !broker->endpoint.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::app::manager_errors::accessibility_endpoint_unavailable);
    }
    return std::move(broker->endpoint);
}

[[nodiscard]] os::supervisor::ServiceLaunchConfig make_config(
    const char* executable,
    int broker_capability_fd) noexcept {
    return os::supervisor::ServiceLaunchConfig{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = os::accessibility::accessibility_service_id,
            .principal_id = os::accessibility::accessibility_service_principal,
            .user_id = os::core::UserId{0U},
            .name = "system.accessibility",
            .restart_policy = os::supervisor::RestartPolicy::on_failure,
            .restart_delay_ms = 10U,
            .max_restarts_in_window = 3U,
            .restart_window_ms = 2000U,
            .readiness_timeout_ms = 2000U,
        },
        .executable_path = executable,
        .private_capability_fd = broker_capability_fd,
    };
}

[[noreturn]] void run_application_server(
    os::ipc::Channel channel,
    os::core::PeerIdentity service_identity) {
    os::ui::SemanticTree tree{rect(0U, 0U, 360U, 800U)};
    if (!tree.valid()) ::_exit(40);
    const auto actions =
        os::ui::action_mask(os::ui::UiAction::activate) |
        os::ui::action_mask(os::ui::UiAction::focus);
    auto button = tree.add(
        tree.root(),
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(24U, 180U, 180U, 56U),
            .actions = actions,
            .label = text("Continue"),
        });
    if (!button) ::_exit(41);

    os::ui::AccessibilityBridgeAuthority authority{
        tree,
        os::accessibility::accessibility_service_principal,
        os::ui::AccessibilitySessionId{session_value},
    };
    if (!authority.valid()) ::_exit(42);
    os::accessibility::AccessibilitySessionServer server{authority, service_identity};
    if (!server.valid()) ::_exit(43);

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto snapshot = server.dispatch_once(channel, scratch);
    if (!snapshot) ::_exit(44);
    auto action = server.dispatch_once(channel, scratch);
    if (!action) ::_exit(45);
    auto focused = tree.focused_node();
    if (!focused || focused.value() != button.value().id) ::_exit(46);
    ::_exit(0);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);

    auto broker_pair_result = os::ipc::Channel::create_local_pair();
    assert(broker_pair_result);
    auto broker_pair = std::move(broker_pair_result).value();

    os::supervisor::ProcessAuthority authority;
    os::supervisor::Supervisor supervisor{
        make_config(argv[1], broker_pair[1].native_fd()),
        authority,
    };
    assert(supervisor.start());
    const auto service_status = supervisor.status();
    assert(service_status.state == os::supervisor::ServiceState::running);
    assert(service_status.identity.principal ==
        os::accessibility::accessibility_service_principal);

    // Publish the trusted admin process into the service's generation-local
    // identity registry before it receives any administration request.
    auto admin_record = supervisor.register_process(
        ::getpid(),
        os::accessibility::accessibility_admin_principal,
        os::core::UserId{0U});
    assert(admin_record);

    auto app_pair_result = os::ipc::Channel::create_local_pair();
    assert(app_pair_result);
    auto app_pair = std::move(app_pair_result).value();

    const pid_t app_child = ::fork();
    assert(app_child >= 0);
    if (app_child == 0) {
        broker_pair[0].close();
        broker_pair[1].close();
        app_pair[1].close();
        run_application_server(std::move(app_pair[0]), service_status.identity);
    }
    app_pair[0].close();

    const pid_t broker_child = ::fork();
    assert(broker_child >= 0);
    if (broker_child == 0) {
        broker_pair[1].close();
        FixedResolver resolver{service_status.native_pid, service_status.identity};
        FakeBroker broker{};
        broker.target = application_peer;
        broker.endpoint.session_id = session_value;
        broker.endpoint.application = application_peer;
        broker.endpoint.channel = std::move(app_pair[1]);
        const os::app::AccessibilityEndpointBrokerBackend backend{
            .context = &broker,
            .claim = claim_endpoint,
        };
        os::app::AccessibilityBrokerControlServer server{backend, resolver};
        if (!server.valid()) ::_exit(50);
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto result = server.dispatch_once(broker_pair[0], scratch);
        if (!result) ::_exit(51);
        if (broker.endpoint.valid()) ::_exit(52);
        ::_exit(0);
    }
    broker_pair[0].close();
    app_pair[1].close();

    auto admin_channel_result = supervisor.connect();
    assert(admin_channel_result);
    auto admin_channel = std::move(admin_channel_result).value();
    os::accessibility::AccessibilityServiceClient client{admin_channel};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto claimed = client.claim(application_peer, scratch);
    assert(claimed);
    assert(claimed.value().session_id == session_value);
    assert(claimed.value().application == application_peer);

    os::ui::AccessibilitySessionSnapshot snapshot{};
    assert(client.snapshot(application_peer, snapshot, scratch));
    assert(snapshot.session == os::ui::AccessibilitySessionId{session_value});
    assert(snapshot.snapshot.revision != 0U);

    os::ui::UiNodeId button{};
    for (std::size_t index = 0U; index < snapshot.snapshot.semantic.count; ++index) {
        const auto& node = snapshot.snapshot.semantic.nodes[index];
        if (node.role == os::ui::UiRole::button) {
            button = node.id;
            break;
        }
    }
    assert(button.value() != 0U);

    auto action = client.dispatch_action(
        os::accessibility::AccessibilityServiceActionRequest{
            .application = application_peer,
            .snapshot_revision = snapshot.snapshot.revision,
            .target = button,
            .action = os::ui::UiAction::focus,
        },
        scratch);
    assert(action);
    assert(action.value().target == button);
    assert(action.value().action == os::ui::UiAction::focus);
    assert(client.release(application_peer, scratch));

    int broker_status = 0;
    assert(::waitpid(broker_child, &broker_status, 0) == broker_child);
    assert(WIFEXITED(broker_status));
    assert(WEXITSTATUS(broker_status) == 0);

    int app_status = 0;
    assert(::waitpid(app_child, &app_status, 0) == app_child);
    assert(WIFEXITED(app_status));
    assert(WEXITSTATUS(app_status) == 0);

    assert(supervisor.unregister_process(admin_record.value().peer.process));
    return 0;
}
