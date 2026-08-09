#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/identity.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/error.hpp>
#include <os/keys/id_source.hpp>
#include <os/keys/key.hpp>
#include <os/keys/registry.hpp>
#include <os/keys/service.hpp>
#include <os/keys/testing/openssl_provider.hpp>

namespace {

constexpr os::core::PeerIdentity owner_identity{
    .principal = os::core::PrincipalId{0xAA06000000000001ULL, 0xBB06000000000001ULL},
    .user = os::core::UserId{60U},
    .process = os::core::ProcessId{601U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    explicit TestIdentityResolver(pid_t owner_pid) noexcept : owner_pid_(owner_pid) {}

    os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id != static_cast<std::int64_t>(owner_pid_) ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid())) {
            return os::core::make_error(
                os::core::ErrorDomain::security,
                os::core::errors::security::credential_mismatch);
        }
        return owner_identity;
    }

private:
    pid_t owner_pid_ {-1};
};

class TestIdSource final : public os::keys::KeyIdSource {
public:
    os::core::Result<os::keys::KeyId> next() noexcept override {
        return os::keys::KeyId{0x5250434145414454ULL, next_++};
    }

private:
    std::uint64_t next_ {1U};
};

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

[[noreturn]] void run_server(os::ipc::Channel channel, pid_t owner_pid) {
    TestIdentityResolver resolver{owner_pid};
    os::keys::testing::OpenSslTestKeyProvider provider;
    TestIdSource ids;
    os::keys::KeyRegistry registry{provider};
    os::keys::KeyService service{channel, resolver, registry, ids};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    for (;;) {
        auto result = service.dispatch_once(scratch, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                std::_Exit(0);
            }
            std::_Exit(20);
        }
    }
}

} // namespace

int main() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();

    const pid_t parent_pid = ::getpid();
    const pid_t server = ::fork();
    assert(server >= 0);
    if (server == 0) {
        channels[0].close();
        run_server(std::move(channels[1]), parent_pid);
    }

    channels[1].close();
    os::ipc::ClientConnection connection{channels[0]};
    os::keys::KeyClient keys{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto key_result = keys.create_application_data_key(scratch);
    assert(key_result);
    auto key = std::move(key_result).value();
    const auto key_id = key.descriptor().id;
    assert(key.descriptor().version == 1U);

    auto duplicate_result = keys.open(key_id, scratch);
    assert(duplicate_result);
    auto duplicate = std::move(duplicate_result).value();

    constexpr std::string_view plaintext_text = "ENML typed Key Service authenticated payload";
    constexpr std::string_view aad_text = "principal-profile:60;object:data-v1";
    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope{};
    std::array<std::byte, os::keys::max_key_plaintext_bytes> plaintext_output{};

    auto encrypted = key.encrypt(
        as_bytes(plaintext_text),
        as_bytes(aad_text),
        envelope,
        scratch);
    assert(encrypted);
    assert(encrypted.value() == os::keys::ciphertext_fixed_overhead + plaintext_text.size());

    const os::core::ByteSpan envelope_view{envelope.data(), encrypted.value()};
    auto parsed = os::keys::parse_ciphertext_envelope_v1(envelope_view);
    assert(parsed);
    assert(parsed.value().header.key_id == key_id);
    assert(parsed.value().header.key_version == key.descriptor().version);
    assert(parsed.value().header.profile == os::keys::CryptoProfileId::aes_256_gcm_v1);

    auto decrypted = duplicate.decrypt(
        envelope_view,
        as_bytes(aad_text),
        plaintext_output,
        scratch);
    assert(decrypted);
    assert(decrypted.value() == plaintext_text.size());
    assert(std::equal(
        plaintext_output.begin(),
        plaintext_output.begin() + static_cast<std::ptrdiff_t>(decrypted.value()),
        as_bytes(plaintext_text).begin()));

    auto wrong_aad = duplicate.decrypt(
        envelope_view,
        as_bytes("principal-profile:wrong"),
        plaintext_output,
        scratch);
    assert(!wrong_aad);
    assert(wrong_aad.error().domain == os::core::ErrorDomain::key);
    assert(wrong_aad.error().code == os::keys::errors::authentication_failed);

    auto tampered_tag = envelope;
    tampered_tag[os::keys::ciphertext_header_bytes + os::keys::aead_nonce_bytes] ^= std::byte{0x01};
    auto bad_tag = duplicate.decrypt(
        {tampered_tag.data(), encrypted.value()},
        as_bytes(aad_text),
        plaintext_output,
        scratch);
    assert(!bad_tag);
    assert(bad_tag.error().domain == os::core::ErrorDomain::key);
    assert(bad_tag.error().code == os::keys::errors::authentication_failed);

    auto tampered_ciphertext = envelope;
    tampered_ciphertext[os::keys::ciphertext_fixed_overhead] ^= std::byte{0x80};
    auto bad_ciphertext = duplicate.decrypt(
        {tampered_ciphertext.data(), encrypted.value()},
        as_bytes(aad_text),
        plaintext_output,
        scratch);
    assert(!bad_ciphertext);
    assert(bad_ciphertext.error().domain == os::core::ErrorDomain::key);
    assert(bad_ciphertext.error().code == os::keys::errors::authentication_failed);

    auto wrong_key = envelope;
    wrong_key[16] ^= std::byte{0x01};
    auto bad_key = duplicate.decrypt(
        {wrong_key.data(), encrypted.value()},
        as_bytes(aad_text),
        plaintext_output,
        scratch);
    assert(!bad_key);
    assert(bad_key.error().domain == os::core::ErrorDomain::key);
    assert(bad_key.error().code == os::keys::errors::key_id_mismatch);

    auto wrong_version = envelope;
    wrong_version[12] ^= std::byte{0x02};
    auto bad_version = duplicate.decrypt(
        {wrong_version.data(), encrypted.value()},
        as_bytes(aad_text),
        plaintext_output,
        scratch);
    assert(!bad_version);
    assert(bad_version.error().domain == os::core::ErrorDomain::key);
    assert(bad_version.error().code == os::keys::errors::key_version_mismatch);

    std::array<std::byte, os::keys::max_key_plaintext_bytes> maximum_plaintext{};
    for (std::size_t index = 0U; index < maximum_plaintext.size(); ++index) {
        maximum_plaintext[index] = static_cast<std::byte>(index & 0xFFU);
    }
    std::array<std::byte, os::keys::max_key_aad_bytes> maximum_aad{};
    maximum_aad.fill(std::byte{0xA5});
    auto maximum_encrypted = key.encrypt(
        maximum_plaintext,
        maximum_aad,
        envelope,
        scratch);
    assert(maximum_encrypted);
    assert(maximum_encrypted.value() == os::keys::max_ciphertext_envelope_bytes);
    auto maximum_decrypted = duplicate.decrypt(
        {envelope.data(), maximum_encrypted.value()},
        maximum_aad,
        plaintext_output,
        scratch);
    assert(maximum_decrypted);
    assert(maximum_decrypted.value() == maximum_plaintext.size());
    assert(std::equal(
        maximum_plaintext.begin(), maximum_plaintext.end(), plaintext_output.begin()));

    std::array<std::byte, os::keys::max_key_plaintext_bytes + 1U> oversized_plaintext{};
    auto oversized = key.encrypt(oversized_plaintext, {}, envelope, scratch);
    assert(!oversized);
    assert(oversized.error().domain == os::core::ErrorDomain::key);
    assert(oversized.error().code == os::keys::errors::too_large);

    assert(key.destroy(scratch));
    auto stale = duplicate.decrypt(
        {envelope.data(), maximum_encrypted.value()},
        maximum_aad,
        plaintext_output,
        scratch);
    assert(!stale);
    assert(stale.error().domain == os::core::ErrorDomain::ipc);
    assert(stale.error().code == os::ipc::errors::peer_died);

    auto reopened = keys.open(key_id, scratch);
    assert(!reopened);
    assert(reopened.error().domain == os::core::ErrorDomain::key);
    assert(reopened.error().code == os::keys::errors::destroyed);

    channels[0].close();
    int status = 0;
    assert(::waitpid(server, &status, 0) == server);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
