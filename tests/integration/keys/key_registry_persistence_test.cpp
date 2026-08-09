#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/core/span.hpp>
#include <os/keys/error.hpp>
#include <os/keys/persistence.hpp>
#include <os/keys/testing/openssl_provider.hpp>

namespace {

constexpr os::keys::KeyOwner owner{
    .principal = os::core::PrincipalId{0xA770000000000001ULL, 0xB770000000000001ULL},
    .user = os::core::UserId{0x10000004DULL},
};
constexpr os::keys::KeyId durable_key{0x44555241424C4501ULL, 1U};
constexpr os::keys::KeyId destroyed_key{0x44555241424C4502ULL, 2U};

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

[[nodiscard]] os::core::NativeHandle open_directory(const char* path) noexcept {
    return os::core::NativeHandle(::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC));
}

struct SealedFixture final {
    std::array<std::byte, 128U> ciphertext{};
    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    std::size_t size {0U};
};

[[nodiscard]] SealedFixture seal_text(
    os::keys::KeyStore& store,
    std::uint32_t version,
    std::string_view text) {
    SealedFixture fixture;
    auto sealed = store.seal(
        owner,
        durable_key,
        version,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        as_bytes("registry-envelope-aad"),
        as_bytes("principal-binding-aad"),
        as_bytes(text),
        fixture.ciphertext,
        fixture.nonce,
        fixture.tag);
    assert(sealed);
    assert(sealed.value() == text.size());
    fixture.size = sealed.value();
    return fixture;
}

void assert_opens(
    os::keys::KeyStore& store,
    std::uint32_t version,
    const SealedFixture& fixture,
    std::string_view expected) {
    std::array<std::byte, 128U> plaintext{};
    auto opened = store.open(
        owner,
        durable_key,
        version,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        as_bytes("registry-envelope-aad"),
        as_bytes("principal-binding-aad"),
        fixture.nonce,
        fixture.tag,
        {fixture.ciphertext.data(), fixture.size},
        plaintext);
    assert(opened);
    assert(opened.value() == expected.size());
    assert(std::equal(
        plaintext.begin(),
        plaintext.begin() + static_cast<std::ptrdiff_t>(opened.value()),
        as_bytes(expected).begin()));
}

} // namespace

int main() {
    char directory_template[] = "/tmp/enml-key-registry-XXXXXX";
    char* directory_path = ::mkdtemp(directory_template);
    assert(directory_path != nullptr);

    char outside_template[] = "/tmp/enml-key-registry-outside-XXXXXX";
    const int outside_fd = ::mkstemp(outside_template);
    assert(outside_fd >= 0);
    constexpr std::string_view sentinel = "outside-must-not-change";
    assert(::write(outside_fd, sentinel.data(), sentinel.size()) ==
        static_cast<ssize_t>(sentinel.size()));
    assert(::close(outside_fd) == 0);

    SealedFixture v1{};
    SealedFixture v2{};

    {
        os::keys::testing::OpenSslTestKeyProvider provider;
        auto opened = os::keys::PersistentKeyRegistry::open(
            open_directory(directory_path), provider);
        assert(opened);
        auto store = std::move(opened).value();

        auto created = store.create(
            owner,
            durable_key,
            os::keys::KeyPurpose::application_data_aead,
            os::keys::key_rights::all);
        assert(created);
        assert(created.value().version == 1U);
        v1 = seal_text(store, 1U, "ciphertext-created-under-v1");

        // A pre-existing symlink at the temporary publication name must be
        // removed rather than followed. Rotation still commits, and the target
        // outside the authorized state directory remains untouched.
        std::array<char, 512U> temp_link{};
        const int formatted = std::snprintf(
            temp_link.data(),
            temp_link.size(),
            "%s/.key-registry-v1.tmp",
            directory_path);
        assert(formatted > 0);
        assert(static_cast<std::size_t>(formatted) < temp_link.size());
        assert(::symlink(outside_template, temp_link.data()) == 0);

        auto rotated = store.rotate(owner, durable_key);
        assert(rotated);
        assert(rotated.value().version == 2U);
        assert(store.version_count(durable_key) == 2U);
        v2 = seal_text(store, 2U, "ciphertext-created-under-v2");

        auto doomed = store.create(
            owner,
            destroyed_key,
            os::keys::KeyPurpose::application_data_aead,
            os::keys::key_rights::all);
        assert(doomed);
        assert(store.destroy(owner, destroyed_key));

        struct stat state_metadata {};
        assert(::fstatat(
            open_directory(directory_path).native(),
            "key-registry-v1.bin",
            &state_metadata,
            AT_SYMLINK_NOFOLLOW) == 0);
        assert(S_ISREG(state_metadata.st_mode));
        assert((state_metadata.st_mode & 0777) == 0600);
    }

    {
        const int fd = ::open(outside_template, O_RDONLY | O_CLOEXEC);
        assert(fd >= 0);
        std::array<char, 64U> contents{};
        const auto read_count = ::read(fd, contents.data(), contents.size());
        assert(read_count == static_cast<ssize_t>(sentinel.size()));
        assert(std::string_view(contents.data(), sentinel.size()) == sentinel);
        assert(::close(fd) == 0);
    }

    {
        // A fresh provider and a freshly opened registry simulate Key Service
        // restart. Provider references are reconstructed from opaque wrapped
        // blobs; the process-local references from the first provider are gone.
        os::keys::testing::OpenSslTestKeyProvider provider;
        auto opened = os::keys::PersistentKeyRegistry::open(
            open_directory(directory_path), provider);
        assert(opened);
        auto store = std::move(opened).value();

        auto descriptor = store.describe(owner, durable_key);
        assert(descriptor);
        assert(descriptor.value().version == 2U);
        assert(store.version_count(durable_key) == 2U);
        assert_opens(store, 1U, v1, "ciphertext-created-under-v1");
        assert_opens(store, 2U, v2, "ciphertext-created-under-v2");

        auto tombstone = store.describe(owner, destroyed_key);
        assert(!tombstone);
        assert(tombstone.error() == os::keys::key_error(os::keys::errors::destroyed));
        auto forbidden_reuse = store.create(
            owner,
            destroyed_key,
            os::keys::KeyPurpose::application_data_aead,
            os::keys::key_rights::all);
        assert(!forbidden_reuse);
        assert(forbidden_reuse.error() ==
            os::keys::key_error(os::keys::errors::duplicate_key_id));

        auto rotated = store.rotate(owner, durable_key);
        assert(rotated);
        assert(rotated.value().version == 3U);
    }

    {
        os::keys::testing::OpenSslTestKeyProvider provider;
        auto opened = os::keys::PersistentKeyRegistry::open(
            open_directory(directory_path), provider);
        assert(opened);
        auto store = std::move(opened).value();
        auto descriptor = store.describe(owner, durable_key);
        assert(descriptor);
        assert(descriptor.value().version == 3U);
        assert(store.version_count(durable_key) == 3U);
        assert_opens(store, 1U, v1, "ciphertext-created-under-v1");
        assert_opens(store, 2U, v2, "ciphertext-created-under-v2");
    }

    std::array<char, 512U> state_path{};
    const int state_formatted = std::snprintf(
        state_path.data(), state_path.size(), "%s/key-registry-v1.bin", directory_path);
    assert(state_formatted > 0);
    assert(static_cast<std::size_t>(state_formatted) < state_path.size());
    (void)::unlink(state_path.data());
    (void)::unlink(outside_template);
    assert(::rmdir(directory_path) == 0);
    return 0;
}
