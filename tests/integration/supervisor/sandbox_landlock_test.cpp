#include <cassert>
#include <cerrno>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/sandbox/sandbox.hpp>

int main() {
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        auto policy = os::sandbox::SandboxPolicyV1{};
        policy.require_seccomp = false;
        policy.require_landlock = true;
        auto result = os::sandbox::apply_before_exec("/proc/self/exe", policy);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::security &&
                result.error().code == os::core::errors::security::sandbox_not_supported) {
                _exit(77);
            }
            _exit(2);
        }

        errno = 0;
        const int fd = ::open("/tmp/emnl-landlock-must-not-create", O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
        if (fd >= 0) {
            (void)::close(fd);
            (void)::unlink("/tmp/emnl-landlock-must-not-create");
            _exit(3);
        }
        if (errno != EACCES && errno != EPERM) _exit(4);
        _exit(0);
    }

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    if (!WIFEXITED(status)) return 5;
    return WEXITSTATUS(status);
}
