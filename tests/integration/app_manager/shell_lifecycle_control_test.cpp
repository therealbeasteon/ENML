#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/app/shell_lifecycle_control.hpp>
#include <os/core/error.hpp>
#include <os/core/platform_principals.hpp>
#include <os/ipc/constants.hpp>
#include <os/package/package.hpp>

namespace {

constexpr os::core::PrincipalId ordinary_principal{
    0x4F5244494E415259ULL,
    0x5348454C4C000001ULL,
};

os::package::ApplicationIdentity application_identity(const char* package_name, std::byte marker) {
    auto package = os::package::PackageId::parse(package_name);
    assert(package);
    os::package::SignerLineageId signer{};
    signer.bytes[0] = marker;
    assert(signer.valid());
    return os::package::ApplicationIdentity{
        .package_id = package.value(),
        .signer_lineage = signer,
    };
}

os::core::PeerIdentity application_peer(std::uint64_t serial) {
    return os::core::PeerIdentity{
        .principal = os::core::PrincipalId{
            0x4150504C49434154ULL,
            0x494F4E0000000000ULL + serial,
        },
        .user = os::core::UserId{77U},
        .process = os::core::ProcessId{500U + serial},
    };
}

os::app::ApplicationLifecycleSnapshot lifecycle_snapshot() {
    os::app::ApplicationLifecycleSnapshot snapshot{};
    snapshot.revision = 9U;
    snapshot.count = 2U;
    snapshot.applications[0] = os::app::ApplicationLifecycleRecord{
        .instance = os::core::ApplicationInstanceId{3U},
        .application = application_identity("com.enml.fixture.alpha", std::byte{0x31}),
        .identity = application_peer(1U),
    };
    snapshot.applications[1] = os::app::ApplicationLifecycleRecord{
        .instance = os::core::ApplicationInstanceId{8U},
        .application = application_identity("com.enml.fixture.beta", std::byte{0x42}),
        .identity = application_peer(2U),
    };
    return snapshot;
}

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

struct FakeLifecycle final {
    os::app::ApplicationLifecycleSnapshot snapshot {};
    std::uint32_t calls {0U};
};

[[nodiscard]] os::core::Result<os::app::ApplicationLifecycleSnapshot> read_snapshot(
    void* context) noexcept {
    auto* lifecycle = static_cast<FakeLifecycle*>(context);
    if (lifecycle == nullptr) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::invalid_request);
    }
    ++lifecycle->calls;
    return lifecycle->snapshot;
}

os::core::PeerIdentity service_peer(os::core::PrincipalId principal, std::uint64_t process) {
    return os::core::PeerIdentity{
        .principal = principal,
        .user = os::core::UserId{0U},
        .process = os::core::ProcessId{process},
    };
}

} // namespace

int main() {
    TestIdentityResolver resolver{};
    FakeLifecycle lifecycle{.snapshot = lifecycle_snapshot()};
    os::app::ShellLifecycleControlServer server{
        os::app::ShellLifecycleBackend{
            .context = &lifecycle,
            .snapshot = read_snapshot,
        },
        resolver,
    };
    assert(server.valid());

    auto trusted_pair_result = os::ipc::Channel::create_local_pair();
    assert(trusted_pair_result);
    auto trusted_pair = std::move(trusted_pair_result).value();

    const pid_t trusted_child = ::fork();
    assert(trusted_child >= 0);
    if (trusted_child == 0) {
        trusted_pair[0].close();
        os::app::ShellLifecycleControlClient client{trusted_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto snapshot = client.snapshot(scratch);
        if (!snapshot) ::_exit(20);
        if (snapshot.value().revision != 9U || snapshot.value().count != 2U) ::_exit(21);
        if (snapshot.value().applications[0].instance != os::core::ApplicationInstanceId{3U} ||
            snapshot.value().applications[1].instance != os::core::ApplicationInstanceId{8U}) {
            ::_exit(22);
        }
        if (snapshot.value().applications[0].identity != application_peer(1U) ||
            snapshot.value().applications[1].identity != application_peer(2U)) {
            ::_exit(23);
        }
        ::_exit(0);
    }

    trusted_pair[1].close();
    resolver.expect(trusted_child, service_peer(os::core::shell_service_principal, 9001U));
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto handled = server.dispatch_once(trusted_pair[0], scratch);
    assert(handled);
    assert(lifecycle.calls == 1U);

    int trusted_status = 0;
    assert(::waitpid(trusted_child, &trusted_status, 0) == trusted_child);
    assert(WIFEXITED(trusted_status));
    assert(WEXITSTATUS(trusted_status) == 0);

    // Possession of the private endpoint and knowledge of the control ServiceId
    // are not enough. A different kernel sender is denied before the snapshot
    // backend runs, preventing lifecycle enumeration through error differences.
    auto denied_pair_result = os::ipc::Channel::create_local_pair();
    assert(denied_pair_result);
    auto denied_pair = std::move(denied_pair_result).value();

    const pid_t ordinary_child = ::fork();
    assert(ordinary_child >= 0);
    if (ordinary_child == 0) {
        denied_pair[0].close();
        os::app::ShellLifecycleControlClient client{denied_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> child_scratch{};
        auto denied = client.snapshot(child_scratch);
        if (denied ||
            denied.error().domain != os::core::ErrorDomain::service ||
            denied.error().code != os::core::errors::service::access_denied) {
            ::_exit(30);
        }
        ::_exit(0);
    }

    denied_pair[1].close();
    resolver.expect(ordinary_child, service_peer(ordinary_principal, 9002U));
    auto denied_handled = server.dispatch_once(denied_pair[0], scratch);
    assert(denied_handled);
    assert(lifecycle.calls == 1U);

    int ordinary_status = 0;
    assert(::waitpid(ordinary_child, &ordinary_status, 0) == ordinary_child);
    assert(WIFEXITED(ordinary_status));
    assert(WEXITSTATUS(ordinary_status) == 0);

    // Even the trusted caller cannot make ambiguous lifecycle state cross the
    // boundary. Duplicate exact identities are rejected by server-side record
    // validation before serialization.
    auto malformed_pair_result = os::ipc::Channel::create_local_pair();
    assert(malformed_pair_result);
    auto malformed_pair = std::move(malformed_pair_result).value();
    lifecycle.snapshot.applications[1].identity = lifecycle.snapshot.applications[0].identity;

    const pid_t malformed_child = ::fork();
    assert(malformed_child >= 0);
    if (malformed_child == 0) {
        malformed_pair[0].close();
        os::app::ShellLifecycleControlClient client{malformed_pair[1]};
        std::array<std::byte, os::ipc::max_wire_packet_size> child_scratch{};
        auto malformed = client.snapshot(child_scratch);
        if (malformed || malformed.error().domain != os::core::ErrorDomain::ipc) {
            ::_exit(40);
        }
        ::_exit(0);
    }

    malformed_pair[1].close();
    resolver.expect(malformed_child, service_peer(os::core::shell_service_principal, 9003U));
    auto malformed_handled = server.dispatch_once(malformed_pair[0], scratch);
    assert(malformed_handled);
    assert(lifecycle.calls == 2U);

    int malformed_status = 0;
    assert(::waitpid(malformed_child, &malformed_status, 0) == malformed_child);
    assert(WIFEXITED(malformed_status));
    assert(WEXITSTATUS(malformed_status) == 0);

    return 0;
}
