#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/sandbox/sandbox.hpp>

namespace {

int child_probe(int executable_fd, int data_fd, int outside_fd) {
    os::sandbox::SandboxPolicyV1 policy{};
    policy.require_landlock = true;
    policy.require_seccomp = false;
    policy.max_open_files = 32U;
    policy.max_processes = 4U;
    policy.max_file_size_bytes = 1024U * 1024U;

    auto applied = os::sandbox::apply_application_before_exec(
        os::sandbox::ApplicationSandboxHandlesV1{
            .executable_fd = executable_fd,
            .private_data_directory_fd = data_fd,
        },
        policy);
    if (!applied) {
        if (applied.error().domain == os::core::ErrorDomain::security &&
            applied.error().code == os::core::errors::security::sandbox_not_supported) {
            return 77;
        }
        return 10;
    }

    const int allowed = ::openat(data_fd, "allowed", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (allowed < 0) return 11;
    static_cast<void>(::write(allowed, "ok", 2U));
    if (::close(allowed) != 0) return 12;

    errno = 0;
    const int denied = ::openat(outside_fd, "escape", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (denied >= 0) {
        (void)::close(denied);
        return 13;
    }
    if (errno != EACCES && errno != EPERM) return 14;
    return 0;
}

} // namespace

int main() {
    char data_template[] = "/tmp/emnl-landlock-data-XXXXXX";
    char outside_template[] = "/tmp/emnl-landlock-outside-XXXXXX";
    char* data_path = ::mkdtemp(data_template);
    char* outside_path = ::mkdtemp(outside_template);
    assert(data_path != nullptr && outside_path != nullptr);

    const int executable_fd = ::open("/proc/self/exe", O_PATH | O_CLOEXEC);
    const int data_fd = ::open(data_path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    const int outside_fd = ::open(outside_path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    assert(executable_fd >= 0 && data_fd >= 0 && outside_fd >= 0);

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        const int code = child_probe(executable_fd, data_fd, outside_fd);
        std::_Exit(code);
    }

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(::close(executable_fd) == 0);
    assert(::close(data_fd) == 0);
    assert(::close(outside_fd) == 0);

    static_cast<void>(::unlinkat(AT_FDCWD, (std::string(data_path) + "/allowed").c_str(), 0));
    static_cast<void>(::unlinkat(AT_FDCWD, (std::string(outside_path) + "/escape").c_str(), 0));
    assert(::rmdir(data_path) == 0);
    assert(::rmdir(outside_path) == 0);

    if (!WIFEXITED(status)) return 20;
    return WEXITSTATUS(status);
}
