#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/package/persistence.hpp>

namespace pkg = os::package;

namespace {

pkg::ApplicationIdentity make_application(std::uint8_t signer_seed) {
    auto package_id = pkg::PackageId::parse("com.emnl.notes");
    assert(package_id);
    pkg::SignerLineageId signer {};
    signer.bytes[0] = static_cast<std::byte>(signer_seed);
    return pkg::ApplicationIdentity{package_id.value(), signer};
}

pkg::ContentDigest make_digest(std::uint8_t seed) {
    pkg::ContentDigest digest {};
    digest.bytes[0] = static_cast<std::byte>(seed);
    digest.bytes[31] = static_cast<std::byte>(static_cast<std::uint8_t>(seed ^ 0xA5U));
    return digest;
}

pkg::PackageGenerationRecord make_generation(
    const pkg::ApplicationIdentity& application,
    std::uint64_t generation,
    std::uint8_t digest_seed) {
    return pkg::PackageGenerationRecord{
        application,
        pkg::PackageGenerationId(generation),
        make_digest(digest_seed),
    };
}

int open_directory(const char* path) {
    return ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

void write_junk_file(int directory_fd, const char* name) {
    const int fd = ::openat(
        directory_fd,
        name,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        S_IRUSR | S_IWUSR);
    assert(fd >= 0);
    const std::array<std::byte, 7> junk{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF},
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
    };
    const auto written = ::write(fd, junk.data(), junk.size());
    assert(written == static_cast<ssize_t>(junk.size()));
    assert(::fsync(fd) == 0);
    assert(::close(fd) == 0);
}

pkg::PersistentPackageRegistry reopen_registry(const char* path) {
    const int directory_fd = open_directory(path);
    assert(directory_fd >= 0);
    auto opened = pkg::PersistentPackageRegistry::open(os::core::NativeHandle(directory_fd));
    assert(opened);
    return std::move(opened).value();
}

} // namespace

int main() {
    char directory_template[] = "/tmp/emnl-pkg-registry-XXXXXX";
    char* directory = ::mkdtemp(directory_template);
    assert(directory != nullptr);

    const auto app = make_application(0x11U);
    const auto wrong_signer = make_application(0x22U);
    const auto generation1 = make_generation(app, 1U, 0x31U);
    const auto generation2 = make_generation(app, 2U, 0x32U);
    const auto generation3 = make_generation(app, 3U, 0x33U);

    {
        const int directory_fd = open_directory(directory);
        assert(directory_fd >= 0);
        auto opened = pkg::PersistentPackageRegistry::open(os::core::NativeHandle(directory_fd));
        assert(opened);
        auto store = std::move(opened).value();
        assert(store.application_count() == 0U);

        auto staged1 = store.stage_generation(generation1);
        assert(staged1);
        auto no_active = store.active(app);
        assert(!no_active);
        assert(no_active.error().code == pkg::errors::no_active_generation);

        auto activated1 = store.activate(app, pkg::PackageGenerationId(1U));
        assert(activated1);
        auto active1 = store.active(app);
        assert(active1);
        assert(active1.value().generation.value() == 1U);

        auto staged2 = store.stage_generation(generation2);
        assert(staged2);
        auto still_active1 = store.active(app);
        assert(still_active1);
        assert(still_active1.value().generation.value() == 1U);
    }

    {
        auto store = reopen_registry(directory);
        assert(store.application_count() == 1U);
        auto active = store.active(app);
        assert(active);
        assert(active.value().generation.value() == 1U);
        auto staged = store.generation(app, pkg::PackageGenerationId(2U));
        assert(staged);
        assert(staged.value().content == generation2.content);

        auto activated2 = store.activate(app, pkg::PackageGenerationId(2U));
        assert(activated2);
    }

    {
        auto store = reopen_registry(directory);
        auto active = store.active(app);
        assert(active);
        assert(active.value().generation.value() == 2U);

        auto collision = store.stage_generation(make_generation(wrong_signer, 3U, 0x44U));
        assert(!collision);
        assert(collision.error().code == pkg::errors::package_id_collision);
    }

    {
        const int directory_fd = open_directory(directory);
        assert(directory_fd >= 0);
        struct stat metadata {};
        assert(::fstatat(directory_fd, "registry-v1.bin", &metadata, AT_SYMLINK_NOFOLLOW) == 0);
        assert(S_ISREG(metadata.st_mode));
        assert((metadata.st_mode & 0777) == 0600);

        write_junk_file(directory_fd, ".registry-v1.tmp");
        assert(::close(directory_fd) == 0);

        auto store = reopen_registry(directory);
        auto active = store.active(app);
        assert(active);
        assert(active.value().generation.value() == 2U);
        auto staged3 = store.stage_generation(generation3);
        assert(staged3);

        const int check_fd = open_directory(directory);
        assert(check_fd >= 0);
        errno = 0;
        assert(::faccessat(check_fd, ".registry-v1.tmp", F_OK, AT_SYMLINK_NOFOLLOW) != 0);
        assert(errno == ENOENT);
        assert(::close(check_fd) == 0);
    }

    {
        auto store = reopen_registry(directory);
        auto active = store.active(app);
        assert(active);
        assert(active.value().generation.value() == 2U);
        assert(store.generation(app, pkg::PackageGenerationId(3U)));
    }

    {
        const int directory_fd = open_directory(directory);
        assert(directory_fd >= 0);
        write_junk_file(directory_fd, "registry-v1.bin");
        assert(::close(directory_fd) == 0);

        const int reopen_fd = open_directory(directory);
        assert(reopen_fd >= 0);
        auto malformed = pkg::PersistentPackageRegistry::open(os::core::NativeHandle(reopen_fd));
        assert(!malformed);
        assert(malformed.error().code == pkg::persistence_errors::malformed_snapshot);
    }

    {
        const int directory_fd = open_directory(directory);
        assert(directory_fd >= 0);
        assert(::unlinkat(directory_fd, "registry-v1.bin", 0) == 0);
        assert(::symlinkat("/etc/passwd", directory_fd, "registry-v1.bin") == 0);
        assert(::close(directory_fd) == 0);

        const int reopen_fd = open_directory(directory);
        assert(reopen_fd >= 0);
        auto symlinked = pkg::PersistentPackageRegistry::open(os::core::NativeHandle(reopen_fd));
        assert(!symlinked);
        assert(symlinked.error().code == pkg::persistence_errors::io_failure);
    }

    const int cleanup_fd = open_directory(directory);
    assert(cleanup_fd >= 0);
    static_cast<void>(::unlinkat(cleanup_fd, "registry-v1.bin", 0));
    static_cast<void>(::unlinkat(cleanup_fd, ".registry-v1.tmp", 0));
    assert(::close(cleanup_fd) == 0);
    assert(::rmdir(directory) == 0);
    return 0;
}
