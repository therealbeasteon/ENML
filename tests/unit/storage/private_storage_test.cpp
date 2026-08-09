#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/core/span.hpp>
#include <os/storage/directory.hpp>
#include <os/storage/error.hpp>
#include <os/storage/path.hpp>
#include <os/storage/private_root.hpp>

namespace storage = os::storage;

namespace {

storage::RelativePath path(std::string_view text) {
    auto parsed = storage::RelativePath::parse(text);
    assert(parsed);
    return parsed.value();
}

os::core::ByteSpan bytes(std::string_view text) {
    return os::core::ByteSpan(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
}

os::core::NativeHandle open_directory(const char* directory) {
    const int fd = ::open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
}

void create_host_file(const std::string& filename, std::string_view contents) {
    const int fd = ::open(
        filename.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0600);
    assert(fd >= 0);
    const auto written = ::write(fd, contents.data(), contents.size());
    assert(written == static_cast<ssize_t>(contents.size()));
    assert(::close(fd) == 0);
}

void expect_path_error(std::string_view text, std::uint32_t code) {
    auto parsed = storage::RelativePath::parse(text);
    assert(!parsed);
    assert(parsed.error().domain == os::core::ErrorDomain::storage);
    assert(parsed.error().code == code);
}

} // namespace

int main() {
    assert(path("notes/state.bin").view() == "notes/state.bin");
    assert(path("caf\xC3\xA9/record.txt").valid());

    expect_path_error("", storage::errors::invalid_path);
    expect_path_error("/absolute", storage::errors::invalid_path);
    expect_path_error("trailing/", storage::errors::invalid_path);
    expect_path_error("double//slash", storage::errors::invalid_path);
    expect_path_error(".", storage::errors::path_escape);
    expect_path_error("..", storage::errors::path_escape);
    expect_path_error("a/../b", storage::errors::path_escape);
    expect_path_error("a\\b", storage::errors::invalid_path);

    const std::array<char, 3> embedded_nul{'a', '\0', 'b'};
    expect_path_error(
        std::string_view(embedded_nul.data(), embedded_nul.size()),
        storage::errors::invalid_path);
    const std::array<char, 2> invalid_utf8{
        static_cast<char>(0xC0), static_cast<char>(0xAF)};
    expect_path_error(
        std::string_view(invalid_utf8.data(), invalid_utf8.size()),
        storage::errors::invalid_path);

    const std::string long_segment(storage::max_path_segment_bytes + 1U, 'x');
    expect_path_error(long_segment, storage::errors::segment_too_long);
    const std::string long_path(storage::max_relative_path_bytes + 1U, 'x');
    expect_path_error(long_path, storage::errors::path_too_long);

    char root_template[] = "/tmp/emnl-storage-root-XXXXXX";
    char outside_template[] = "/tmp/emnl-storage-outside-XXXXXX";
    char* root_path = ::mkdtemp(root_template);
    char* outside_path = ::mkdtemp(outside_template);
    assert(root_path != nullptr && outside_path != nullptr);

    const std::string outside_secret = std::string(outside_path) + "/secret";
    create_host_file(outside_secret, "outside");

    const std::string ordinary_file = std::string(root_path) + "/not-a-directory";
    create_host_file(ordinary_file, "x");
    const int ordinary_fd = ::open(ordinary_file.c_str(), O_RDONLY | O_CLOEXEC);
    assert(ordinary_fd >= 0);
    auto invalid_root = storage::PrivateRoot::adopt_authorized_directory(
        os::core::NativeHandle{ordinary_fd});
    assert(!invalid_root);
    assert(invalid_root.error().code == storage::errors::invalid_root);

    auto adopted = storage::PrivateRoot::adopt_authorized_directory(open_directory(root_path));
    assert(adopted);
    auto root = std::move(adopted).value();

    assert(root.create_directory(path("nested")));
    auto nested_result = root.open_directory(path("nested"));
    assert(nested_result);
    auto nested = std::move(nested_result).value();

    auto created = root.open_file(
        path("nested/item.bin"),
        storage::OpenOptions{
            .access = storage::FileAccess::read_write,
            .create = storage::CreateDisposition::create_exclusive,
            .truncate = false,
        });
    assert(created);
    auto file = std::move(created).value();
    assert(file.readable() && file.writable());
    auto write = file.write_at(0U, bytes("hello"));
    assert(write && write.value() == 5U);
    assert(file.sync());
    auto file_size = file.size();
    assert(file_size && file_size.value() == 5U);

    std::array<std::byte, 16> read_buffer{};
    auto read = file.read_at(0U, read_buffer);
    assert(read && read.value() == 5U);
    const auto read_text = std::string_view(
        reinterpret_cast<const char*>(read_buffer.data()), read.value());
    assert(read_text == "hello");

    auto read_only_result = root.open_file(path("nested/item.bin"));
    assert(read_only_result);
    auto read_only = std::move(read_only_result).value();
    assert(read_only.readable() && !read_only.writable());
    auto denied_write = read_only.write_at(0U, bytes("x"));
    assert(!denied_write);
    assert(denied_write.error().code == storage::errors::access_denied);

    auto write_only_result = root.open_file(
        path("nested/item.bin"),
        storage::OpenOptions{
            .access = storage::FileAccess::write_only,
            .create = storage::CreateDisposition::open_existing,
            .truncate = false,
        });
    assert(write_only_result);
    auto write_only = std::move(write_only_result).value();
    auto denied_read = write_only.read_at(0U, read_buffer);
    assert(!denied_read);
    assert(denied_read.error().code == storage::errors::access_denied);

    auto missing = root.open_file(path("missing.bin"));
    assert(!missing);
    assert(missing.error().code == storage::errors::not_found);

    // A subdirectory object is its own rooted authority.
    auto local = nested.open_file(
        path("local.txt"),
        storage::OpenOptions{
            .access = storage::FileAccess::read_write,
            .create = storage::CreateDisposition::create_exclusive,
            .truncate = false,
        });
    assert(local);
    assert(local.value().write_at(0U, bytes("local")));

    const std::string final_link = std::string(root_path) + "/final-link";
    assert(::symlink(outside_secret.c_str(), final_link.c_str()) == 0);
    auto symlink_file = root.open_file(path("final-link"));
    assert(!symlink_file);
    assert(symlink_file.error().code == storage::errors::path_escape);

    const std::string intermediate_link = std::string(root_path) + "/escape";
    assert(::symlink(outside_path, intermediate_link.c_str()) == 0);
    auto escaped_open = root.open_file(path("escape/secret"));
    assert(!escaped_open);
    auto escaped_create = root.open_file(
        path("escape/new-file"),
        storage::OpenOptions{
            .access = storage::FileAccess::read_write,
            .create = storage::CreateDisposition::create_exclusive,
            .truncate = false,
        });
    assert(!escaped_create);
    assert(::access((std::string(outside_path) + "/new-file").c_str(), F_OK) != 0);

    const std::string fifo_path = std::string(root_path) + "/pipe";
    assert(::mkfifo(fifo_path.c_str(), 0600) == 0);
    auto fifo_open = root.open_file(path("pipe"));
    assert(!fifo_open);
    assert(fifo_open.error().code == storage::errors::not_regular_file);

    assert(root.atomic_replace(path("nested/config.bin"), bytes("first")));
    assert(root.atomic_replace(path("nested/config.bin"), bytes("second-version")));
    auto config_result = root.open_file(path("nested/config.bin"));
    assert(config_result);
    auto config = std::move(config_result).value();
    std::array<std::byte, 32> config_buffer{};
    auto config_read = config.read_at(0U, config_buffer);
    assert(config_read && config_read.value() == 14U);
    const auto config_text = std::string_view(
        reinterpret_cast<const char*>(config_buffer.data()), config_read.value());
    assert(config_text == "second-version");

    struct stat config_metadata {};
    assert(::stat((std::string(root_path) + "/nested/config.bin").c_str(), &config_metadata) == 0);
    assert(S_ISREG(config_metadata.st_mode));
    assert((config_metadata.st_mode & 0777) == 0600);

    auto replace_link = root.atomic_replace(path("final-link"), bytes("blocked"));
    assert(!replace_link);
    assert(replace_link.error().code == storage::errors::path_escape);

    assert(root.remove_file(path("nested/config.bin")));
    assert(root.remove_file(path("nested/item.bin")));
    assert(root.remove_file(path("nested/local.txt")));
    assert(root.remove_empty_directory(path("nested")));

    // Outside data was never opened through the private root.
    std::array<char, 16> outside_buffer{};
    const int outside_fd = ::open(outside_secret.c_str(), O_RDONLY | O_CLOEXEC);
    assert(outside_fd >= 0);
    const auto outside_read = ::read(outside_fd, outside_buffer.data(), outside_buffer.size());
    assert(outside_read == 7);
    assert(std::string_view(outside_buffer.data(), 7U) == "outside");
    assert(::close(outside_fd) == 0);

    assert(::unlink(final_link.c_str()) == 0);
    assert(::unlink(intermediate_link.c_str()) == 0);
    assert(::unlink(fifo_path.c_str()) == 0);
    assert(::unlink(ordinary_file.c_str()) == 0);
    assert(::unlink(outside_secret.c_str()) == 0);
    assert(::rmdir(root_path) == 0);
    assert(::rmdir(outside_path) == 0);
    return 0;
}
