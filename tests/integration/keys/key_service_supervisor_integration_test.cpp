#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/control.hpp>
#include <os/keys/error.hpp>
#include <os/keys/service.hpp>
#include <os/supervisor/supervisor.hpp>

#ifndef EMNL_SYSTEM_KEYS_PATH
#error "EMNL_SYSTEM_KEYS_PATH must name the host-CI system.keys executable"
#endif

namespace {

constexpr os::core::PrincipalId key_service_principal{
    0x4B45595345525631ULL,
    0x53595354454D3031ULL,
};
constexpr os::core::PrincipalId application_principal{
    0x4150504B45593031ULL,
    0x5052494E43495031ULL,
};
constexpr os::core::UserId application_user{91U};

void report_error(const char* stage, const os::core::Error& error) noexcept {
    std::fprintf(
        stderr,
        "key_service_supervisor_integration_test stage=%s domain=%u code=%u\n",
        stage,
        static_cast<unsigned>(error.domain),
        static_cast<unsigned>(error.code));
}

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

void wait_for_generation(
    os::supervisor::Supervisor& supervisor,
    std::uint64_t previous_generation) {
    for (std::size_t attempt = 0U; attempt < 300U; ++attempt) {
        auto maintained = supervisor.maintain();
        if (!maintained && maintained.error().domain == os::core::ErrorDomain::service &&
            maintained.error().code == os::core::errors::service::crash_loop) {
            report_error("restart-crash-loop", maintained.error());
            assert(false);
        }
        const auto status = supervisor.status();
        if (status.state == os::supervisor::ServiceState::running &&
            status.generation > previous_generation) {
            return;
        }
        ::usleep(10000U);
    }
    assert(false);
}

void assert_plaintext(
    os::keys::KeyObjectHandle& key,
    os::core::ByteSpan envelope,
    std::string_view expected,
    os::core::MutableByteSpan scratch) {
    std::array<std::byte, os::keys::max_key_plaintext_bytes> plaintext{};
    auto opened = key.decrypt(
        envelope,
        as_bytes("supervised-policy-aad"),
        plaintext,
        scratch);
    if (!opened) report_error("decrypt", opened.error());
    assert(opened);
    assert(opened.value() == expected.size());
    assert(std::equal(
        plaintext.begin(),
        plaintext.begin() + static_cast<std::ptrdiff_t>(opened.value()),
        as_bytes(expected).begin()));
}

} // namespace

int main() {
    char directory_template[] = "/tmp/enml-supervised-keys-XXXXXX";
    char* directory_path = ::mkdtemp(directory_template);
    assert(directory_path != nullptr);

    os::core::NativeHandle state_directory{
        ::open(directory_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    assert(state_directory.valid());

    os::supervisor::ServiceLaunchConfig config{
        .descriptor = os::supervisor::ServiceDescriptorV1{
            .service_id = os::keys::key_service_id,
            .principal_id = key_service_principal,
            .user_id = os::core::UserId{0U},
            .name = "system.keys",
            .restart_policy = os::supervisor::RestartPolicy::on_failure,
            .restart_delay_ms = 20U,
            .max_restarts_in_window = 3U,
            .restart_window_ms = 2000U,
            .readiness_timeout_ms = 2000U,
            .sandbox = {},
        },
        .executable_path = EMNL_SYSTEM_KEYS_PATH,
        .private_state_directory_fd = state_directory.native(),
    };

    os::keys::KeyId key_id{};
    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope_v1{};
    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope_v2{};
    std::size_t envelope_v1_size = 0U;
    std::size_t envelope_v2_size = 0U;

    {
        os::supervisor::Supervisor supervisor{config};
        auto started = supervisor.start();
        if (!started) report_error("start", started.error());
        assert(started);
        assert(supervisor.status().state == os::supervisor::ServiceState::running);

        auto registered = supervisor.register_process(
            ::getpid(), application_principal, application_user);
        if (!registered) report_error("register-process", registered.error());
        assert(registered);
        const auto initial_process = registered.value().peer.process;

        auto private_control_result = supervisor.connect_private_control();
        if (!private_control_result) report_error("connect-private-control", private_control_result.error());
        assert(private_control_result);
        auto private_control = std::move(private_control_result).value();
        os::keys::KeyControlClient policy{private_control};
        std::array<std::byte, os::ipc::max_wire_packet_size> control_scratch{};
        auto enabled = policy.enable_application(
            application_principal, application_user, control_scratch, 1000U);
        if (!enabled) report_error("enable-application", enabled.error());
        assert(enabled);

        auto public_result = supervisor.connect();
        if (!public_result) report_error("connect-public", public_result.error());
        assert(public_result);
        auto public_channel = std::move(public_result).value();
        os::ipc::ClientConnection connection{public_channel};
        os::keys::KeyClient keys{connection};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

        auto created = keys.create_application_data_key(scratch);
        if (!created) report_error("create-key", created.error());
        assert(created);
        auto key = std::move(created).value();
        key_id = key.descriptor().id;
        assert(key.descriptor().version == 1U);

        auto encrypted_v1 = key.encrypt(
            as_bytes("supervised-v1"),
            as_bytes("supervised-policy-aad"),
            envelope_v1,
            scratch);
        if (!encrypted_v1) report_error("encrypt-v1", encrypted_v1.error());
        assert(encrypted_v1);
        envelope_v1_size = encrypted_v1.value();

        auto rotated = key.rotate(scratch);
        if (!rotated) report_error("rotate-v2", rotated.error());
        assert(rotated);
        assert(rotated.value().version == 2U);

        auto encrypted_v2 = key.encrypt(
            as_bytes("supervised-v2"),
            as_bytes("supervised-policy-aad"),
            envelope_v2,
            scratch);
        if (!encrypted_v2) report_error("encrypt-v2", encrypted_v2.error());
        assert(encrypted_v2);
        envelope_v2_size = encrypted_v2.value();

        // Lifecycle revocation disables future acquisition and closes already
        // minted object capabilities without deleting the durable key.
        auto disabled = policy.disable_application(
            application_principal, application_user, control_scratch, 1000U);
        if (!disabled) report_error("disable-application", disabled.error());
        assert(disabled);
        auto revoked_object = key.decrypt(
            {envelope_v2.data(), envelope_v2_size},
            as_bytes("supervised-policy-aad"),
            envelope_v1,
            scratch);
        assert(!revoked_object);
        assert(revoked_object.error().domain == os::core::ErrorDomain::ipc);
        assert(revoked_object.error().code == os::ipc::errors::peer_died);

        auto denied_open = keys.open(key_id, scratch);
        assert(!denied_open);
        assert(denied_open.error() ==
            os::keys::key_error(os::keys::errors::policy_not_registered));

        enabled = policy.enable_application(
            application_principal, application_user, control_scratch, 1000U);
        if (!enabled) report_error("re-enable-application", enabled.error());
        assert(enabled);
        auto reacquired = keys.open(key_id, scratch);
        if (!reacquired) report_error("reacquire-key", reacquired.error());
        assert(reacquired);
        auto reacquired_key = std::move(reacquired).value();
        assert_plaintext(
            reacquired_key,
            {envelope_v1.data(), envelope_v1_size},
            "supervised-v1",
            scratch);
        assert_plaintext(
            reacquired_key,
            {envelope_v2.data(), envelope_v2_size},
            "supervised-v2",
            scratch);

        const auto old_generation = supervisor.status().generation;
        assert(supervisor.terminate(SIGKILL));
        wait_for_generation(supervisor, old_generation);

        // Old-generation connections are never rebound to the restarted
        // service. The supervisor republishes process identity automatically,
        // but application key policy is intentionally absent until system
        // lifecycle code republishes it.
        auto stale_after_restart = reacquired_key.decrypt(
            {envelope_v2.data(), envelope_v2_size},
            as_bytes("supervised-policy-aad"),
            envelope_v1,
            scratch);
        assert(!stale_after_restart);
        assert(stale_after_restart.error().domain == os::core::ErrorDomain::ipc);
        assert(stale_after_restart.error().code == os::ipc::errors::peer_died);

        auto new_public_result = supervisor.connect();
        if (!new_public_result) report_error("connect-public-after-restart", new_public_result.error());
        assert(new_public_result);
        auto new_public = std::move(new_public_result).value();
        os::ipc::ClientConnection new_connection{new_public};
        os::keys::KeyClient new_keys{new_connection};
        auto policy_missing = new_keys.open(key_id, scratch);
        assert(!policy_missing);
        assert(policy_missing.error() ==
            os::keys::key_error(os::keys::errors::policy_not_registered));

        auto republished_identity = supervisor.lookup_process(::getpid());
        if (!republished_identity) report_error("lookup-republished-identity", republished_identity.error());
        assert(republished_identity);
        assert(republished_identity.value().peer.process == initial_process);
        assert(republished_identity.value().peer.principal == application_principal);

        auto new_control_result = supervisor.connect_private_control();
        if (!new_control_result) report_error("connect-control-after-restart", new_control_result.error());
        assert(new_control_result);
        auto new_control = std::move(new_control_result).value();
        os::keys::KeyControlClient restarted_policy{new_control};
        auto restarted_enabled = restarted_policy.enable_application(
            application_principal, application_user, control_scratch, 1000U);
        if (!restarted_enabled) report_error("enable-after-restart", restarted_enabled.error());
        assert(restarted_enabled);

        auto reopened = new_keys.open(key_id, scratch);
        if (!reopened) report_error("reopen-after-restart", reopened.error());
        assert(reopened);
        auto reopened_key = std::move(reopened).value();
        assert(reopened_key.descriptor().version == 2U);
        assert_plaintext(
            reopened_key,
            {envelope_v1.data(), envelope_v1_size},
            "supervised-v1",
            scratch);
        assert_plaintext(
            reopened_key,
            {envelope_v2.data(), envelope_v2_size},
            "supervised-v2",
            scratch);

        auto rotated_v3 = reopened_key.rotate(scratch);
        if (!rotated_v3) report_error("rotate-v3", rotated_v3.error());
        assert(rotated_v3);
        assert(rotated_v3.value().version == 3U);
    }

    state_directory.reset();
    std::array<char, 512U> registry_path{};
    const int registry_length = std::snprintf(
        registry_path.data(), registry_path.size(),
        "%s/key-registry-v1.bin", directory_path);
    assert(registry_length > 0);
    assert(static_cast<std::size_t>(registry_length) < registry_path.size());
    (void)::unlink(registry_path.data());

    std::array<char, 512U> temporary_path{};
    const int temporary_length = std::snprintf(
        temporary_path.data(), temporary_path.size(),
        "%s/.key-registry-v1.tmp", directory_path);
    assert(temporary_length > 0);
    assert(static_cast<std::size_t>(temporary_length) < temporary_path.size());
    (void)::unlink(temporary_path.data());
    assert(::rmdir(directory_path) == 0);
    return 0;
}
