#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <os/app/principal_store.hpp>
#include <os/core/native_handle.hpp>

namespace {

os::core::NativeHandle open_directory(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
}

os::package::ApplicationIdentity make_application(const char* text, std::uint8_t signer_byte) {
    auto package = os::package::PackageId::parse(text);
    assert(package);
    os::package::SignerLineageId signer{};
    signer.bytes[0] = static_cast<std::byte>(signer_byte);
    signer.bytes[31] = static_cast<std::byte>(static_cast<std::uint8_t>(signer_byte ^ 0xA5U));
    return os::package::ApplicationIdentity{package.value(), signer};
}

void remove_store_files(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    static_cast<void>(::unlinkat(fd, "principals-v1.bin", 0));
    static_cast<void>(::unlinkat(fd, ".principals-v1.tmp", 0));
    assert(::close(fd) == 0);
}

} // namespace

int main() {
    char directory_template[] = "/tmp/emnl-principals-XXXXXX";
    char* directory = ::mkdtemp(directory_template);
    assert(directory != nullptr);

    const auto app = make_application("com.emnl.alpha", 0x11U);
    const auto other_signer = make_application("com.emnl.alpha", 0x22U);
    const auto other_app = make_application("com.emnl.beta", 0x33U);
    const os::core::UserId user42{42U};
    const os::core::UserId user43{43U};

    os::core::PrincipalId first_principal{};
    os::core::PrincipalId second_user_principal{};
    {
        auto opened = os::app::ApplicationPrincipalStore::open(open_directory(directory));
        assert(opened);
        auto store = std::move(opened).value();
        assert(store.record_count() == 0U);
        assert(store.next_principal_low() == 1U);

        auto first = store.resolve_or_allocate(app, user42);
        assert(first);
        first_principal = first.value().principal;
        assert(first_principal.high == os::app::application_principal_high_v1);
        assert(first_principal.low == 1U);

        auto repeated = store.resolve_or_allocate(app, user42);
        assert(repeated);
        assert(repeated.value().principal == first_principal);
        assert(store.record_count() == 1U);

        auto second_user = store.resolve_or_allocate(app, user43);
        assert(second_user);
        second_user_principal = second_user.value().principal;
        assert(second_user_principal.low == 2U);
        assert(second_user_principal != first_principal);

        auto alternate_signer = store.resolve_or_allocate(other_signer, user42);
        assert(alternate_signer);
        assert(alternate_signer.value().principal.low == 3U);
        assert(alternate_signer.value().principal != first_principal);
        assert(store.next_principal_low() == 4U);
    }

    {
        auto reopened = os::app::ApplicationPrincipalStore::open(open_directory(directory));
        assert(reopened);
        auto store = std::move(reopened).value();
        assert(store.record_count() == 3U);
        auto first = store.lookup(app, user42);
        auto second_user = store.lookup(app, user43);
        assert(first && second_user);
        assert(first.value().principal == first_principal);
        assert(second_user.value().principal == second_user_principal);
        assert(store.next_principal_low() == 4U);

        auto fourth = store.resolve_or_allocate(other_app, user42);
        assert(fourth);
        assert(fourth.value().principal.low == 4U);
        assert(store.next_principal_low() == 5U);
    }

    struct stat metadata {};
    const std::string snapshot = std::string(directory) + "/principals-v1.bin";
    assert(::stat(snapshot.c_str(), &metadata) == 0);
    assert((metadata.st_mode & 0777) == 0600);

    remove_store_files(directory);
    assert(::rmdir(directory) == 0);

    // A symlink can never become the principal database.
    char symlink_template[] = "/tmp/emnl-principal-link-XXXXXX";
    char* symlink_directory = ::mkdtemp(symlink_template);
    assert(symlink_directory != nullptr);
    const std::string link_path = std::string(symlink_directory) + "/principals-v1.bin";
    assert(::symlink("/dev/null", link_path.c_str()) == 0);
    auto linked = os::app::ApplicationPrincipalStore::open(open_directory(symlink_directory));
    assert(!linked);
    assert(::unlink(link_path.c_str()) == 0);
    assert(::rmdir(symlink_directory) == 0);

    // Truncated state is rejected rather than reset to an empty allocator.
    char corrupt_template[] = "/tmp/emnl-principal-corrupt-XXXXXX";
    char* corrupt_directory = ::mkdtemp(corrupt_template);
    assert(corrupt_directory != nullptr);
    const int corrupt_dir_fd = ::open(corrupt_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(corrupt_dir_fd >= 0);
    const int corrupt_fd = ::openat(
        corrupt_dir_fd,
        "principals-v1.bin",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
        0600);
    assert(corrupt_fd >= 0);
    const std::byte garbage[]{std::byte{0x45}, std::byte{0x50}, std::byte{0x49}};
    assert(::write(corrupt_fd, garbage, sizeof(garbage)) == static_cast<ssize_t>(sizeof(garbage)));
    assert(::close(corrupt_fd) == 0);
    assert(::close(corrupt_dir_fd) == 0);
    auto corrupt = os::app::ApplicationPrincipalStore::open(open_directory(corrupt_directory));
    assert(!corrupt);
    remove_store_files(corrupt_directory);
    assert(::rmdir(corrupt_directory) == 0);

    return 0;
}
