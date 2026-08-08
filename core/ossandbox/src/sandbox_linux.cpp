#include <os/sandbox/sandbox.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <fcntl.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <os/core/error.hpp>

namespace os::sandbox {
namespace {

[[nodiscard]] constexpr os::core::Error sandbox_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] bool set_limit(int resource, rlim_t value) noexcept {
    const rlimit limit{.rlim_cur = value, .rlim_max = value};
    return ::setrlimit(resource, &limit) == 0;
}

[[nodiscard]] bool clear_all_capabilities() noexcept {
#if defined(SYS_capset)
    __user_cap_header_struct header{};
    header.version = _LINUX_CAPABILITY_VERSION_3;
    header.pid = 0;
    std::array<__user_cap_data_struct, 2> data{};
    if (::syscall(SYS_capset, &header, data.data()) != 0) {
        return false;
    }
#else
    return false;
#endif

#ifdef PR_CAP_AMBIENT
    if (::prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0L, 0L, 0L) != 0 && errno != EINVAL) {
        return false;
    }
#endif
    return true;
}

[[nodiscard]] bool add_landlock_path_rule(
    int ruleset_fd,
    const char* path,
    std::uint64_t allowed_access) noexcept {
#if defined(SYS_landlock_add_rule)
    int path_fd = -1;
    do {
        path_fd = ::open(path, O_PATH | O_CLOEXEC);
    } while (path_fd < 0 && errno == EINTR);
    if (path_fd < 0) {
        return errno == ENOENT;
    }

    const landlock_path_beneath_attr rule{
        .allowed_access = allowed_access,
        .parent_fd = path_fd,
    };
    int result = -1;
    do {
        result = static_cast<int>(::syscall(
            SYS_landlock_add_rule,
            ruleset_fd,
            LANDLOCK_RULE_PATH_BENEATH,
            &rule,
            0U));
    } while (result < 0 && errno == EINTR);
    (void)::close(path_fd);
    return result == 0;
#else
    (void)ruleset_fd;
    (void)path;
    (void)allowed_access;
    return false;
#endif
}

[[nodiscard]] bool install_landlock(const char* executable_path) noexcept {
#if defined(SYS_landlock_create_ruleset) && defined(SYS_landlock_restrict_self)
    // Handle all common file mutation rights, and file/directory reads. Rules
    // added below grant only the runtime material needed for a dynamically
    // linked service. Everything else is denied by omission.
    // Landlock access bits were added across kernel UAPI revisions. Cross
    // toolchains can legitimately ship older linux/landlock.h headers than
    // the runtime kernel, so only request optional rights that the build
    // headers actually define. The baseline rights remain mandatory.
    constexpr std::uint64_t optional_handled =
#ifdef LANDLOCK_ACCESS_FS_REFER
        LANDLOCK_ACCESS_FS_REFER |
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
        LANDLOCK_ACCESS_FS_TRUNCATE |
#endif
        0ULL;
    constexpr std::uint64_t handled =
        LANDLOCK_ACCESS_FS_EXECUTE |
        LANDLOCK_ACCESS_FS_WRITE_FILE |
        LANDLOCK_ACCESS_FS_READ_FILE |
        LANDLOCK_ACCESS_FS_READ_DIR |
        LANDLOCK_ACCESS_FS_REMOVE_DIR |
        LANDLOCK_ACCESS_FS_REMOVE_FILE |
        LANDLOCK_ACCESS_FS_MAKE_CHAR |
        LANDLOCK_ACCESS_FS_MAKE_DIR |
        LANDLOCK_ACCESS_FS_MAKE_REG |
        LANDLOCK_ACCESS_FS_MAKE_SOCK |
        LANDLOCK_ACCESS_FS_MAKE_FIFO |
        LANDLOCK_ACCESS_FS_MAKE_BLOCK |
        LANDLOCK_ACCESS_FS_MAKE_SYM |
        optional_handled;

    landlock_ruleset_attr ruleset_attr{};
    ruleset_attr.handled_access_fs = handled;
    int ruleset_fd = -1;
    do {
        ruleset_fd = static_cast<int>(::syscall(
            SYS_landlock_create_ruleset,
            &ruleset_attr,
            sizeof(ruleset_attr),
            0U));
    } while (ruleset_fd < 0 && errno == EINTR);
    if (ruleset_fd < 0) {
        return false;
    }

    constexpr std::uint64_t runtime_read =
        LANDLOCK_ACCESS_FS_EXECUTE |
        LANDLOCK_ACCESS_FS_READ_FILE |
        LANDLOCK_ACCESS_FS_READ_DIR;
    constexpr std::uint64_t file_read = LANDLOCK_ACCESS_FS_READ_FILE;
    constexpr std::uint64_t executable_read =
        LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE;

    bool ok = add_landlock_path_rule(ruleset_fd, executable_path, executable_read);
    // Common dynamic runtime locations. Missing paths are deliberately
    // tolerated so the same profile works across Linux distributions.
    ok = ok && add_landlock_path_rule(ruleset_fd, "/lib", runtime_read);
    ok = ok && add_landlock_path_rule(ruleset_fd, "/lib64", runtime_read);
    ok = ok && add_landlock_path_rule(ruleset_fd, "/usr/lib", runtime_read);
    ok = ok && add_landlock_path_rule(ruleset_fd, "/usr/lib64", runtime_read);
    ok = ok && add_landlock_path_rule(ruleset_fd, "/etc/ld.so.cache", file_read);
    // Sanitizers and a few libc diagnostics read process metadata after exec.
    // This grants read-only access to the process's own proc subtree, not a
    // writable filesystem capability.
    ok = ok && add_landlock_path_rule(ruleset_fd, "/proc/self", LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR);

    int restrict_result = -1;
    if (ok) {
        do {
            restrict_result = static_cast<int>(::syscall(
                SYS_landlock_restrict_self, ruleset_fd, 0U));
        } while (restrict_result < 0 && errno == EINTR);
    }
    (void)::close(ruleset_fd);
    return ok && restrict_result == 0;
#else
    (void)executable_path;
    return false;
#endif
}

#if defined(__x86_64__)
inline constexpr std::uint32_t expected_audit_arch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
inline constexpr std::uint32_t expected_audit_arch = AUDIT_ARCH_AARCH64;
#else
inline constexpr std::uint32_t expected_audit_arch = 0U;
#endif

[[nodiscard]] bool install_seccomp() noexcept {
#if defined(SYS_seccomp) && (defined(__x86_64__) || defined(__aarch64__))
#define EMNL_DENY_SYSCALL(nr) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<std::uint32_t>(nr), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<std::uint32_t>(EPERM))

    const sock_filter filters[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<std::uint32_t>(offsetof(seccomp_data, arch))),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, expected_audit_arch, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<std::uint32_t>(offsetof(seccomp_data, nr))),
#ifdef __NR_ptrace
        EMNL_DENY_SYSCALL(__NR_ptrace),
#endif
#ifdef __NR_mount
        EMNL_DENY_SYSCALL(__NR_mount),
#endif
#ifdef __NR_umount2
        EMNL_DENY_SYSCALL(__NR_umount2),
#endif
#ifdef __NR_pivot_root
        EMNL_DENY_SYSCALL(__NR_pivot_root),
#endif
#ifdef __NR_reboot
        EMNL_DENY_SYSCALL(__NR_reboot),
#endif
#ifdef __NR_kexec_load
        EMNL_DENY_SYSCALL(__NR_kexec_load),
#endif
#ifdef __NR_kexec_file_load
        EMNL_DENY_SYSCALL(__NR_kexec_file_load),
#endif
#ifdef __NR_init_module
        EMNL_DENY_SYSCALL(__NR_init_module),
#endif
#ifdef __NR_finit_module
        EMNL_DENY_SYSCALL(__NR_finit_module),
#endif
#ifdef __NR_delete_module
        EMNL_DENY_SYSCALL(__NR_delete_module),
#endif
#ifdef __NR_swapon
        EMNL_DENY_SYSCALL(__NR_swapon),
#endif
#ifdef __NR_swapoff
        EMNL_DENY_SYSCALL(__NR_swapoff),
#endif
#ifdef __NR_setns
        EMNL_DENY_SYSCALL(__NR_setns),
#endif
#ifdef __NR_unshare
        EMNL_DENY_SYSCALL(__NR_unshare),
#endif
#ifdef __NR_bpf
        EMNL_DENY_SYSCALL(__NR_bpf),
#endif
#ifdef __NR_perf_event_open
        EMNL_DENY_SYSCALL(__NR_perf_event_open),
#endif
#ifdef __NR_open_by_handle_at
        EMNL_DENY_SYSCALL(__NR_open_by_handle_at),
#endif
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
#undef EMNL_DENY_SYSCALL

    const sock_fprog program{
        .len = static_cast<unsigned short>(sizeof(filters) / sizeof(filters[0])),
        .filter = const_cast<sock_filter*>(filters),
    };

    int result = -1;
    do {
        result = static_cast<int>(::syscall(
            SYS_seccomp,
            SECCOMP_SET_MODE_FILTER,
            0U,
            &program));
    } while (result < 0 && errno == EINTR);
    return result == 0;
#else
    return false;
#endif
}

} // namespace

os::core::Result<void>
apply_before_exec(const char* executable_path, const SandboxPolicyV1& policy) noexcept {
    if (!policy.enabled) {
        return {};
    }
    if (executable_path == nullptr || executable_path[0] == '\0' ||
        policy.max_open_files < 5U || policy.max_processes == 0U) {
        return sandbox_error(os::core::errors::security::sandbox_apply_failed);
    }

    if (::prctl(PR_SET_PDEATHSIG, SIGKILL, 0L, 0L, 0L) != 0) {
        return sandbox_error(os::core::errors::security::sandbox_apply_failed);
    }
    (void)::umask(0077);

    if (policy.require_no_new_privs &&
        ::prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) != 0) {
        return sandbox_error(os::core::errors::security::sandbox_apply_failed);
    }

    if (!set_limit(RLIMIT_CORE, 0U) ||
        !set_limit(RLIMIT_NOFILE, static_cast<rlim_t>(policy.max_open_files)) ||
        !set_limit(RLIMIT_NPROC, static_cast<rlim_t>(policy.max_processes)) ||
        !set_limit(RLIMIT_FSIZE, static_cast<rlim_t>(policy.max_file_size_bytes))) {
        return sandbox_error(os::core::errors::security::sandbox_apply_failed);
    }

    if (policy.clear_capabilities && !clear_all_capabilities()) {
        return sandbox_error(os::core::errors::security::sandbox_apply_failed);
    }

    if (policy.require_landlock && !install_landlock(executable_path)) {
        return sandbox_error(os::core::errors::security::sandbox_not_supported);
    }

    if (policy.require_seccomp && !install_seccomp()) {
        return sandbox_error(os::core::errors::security::sandbox_not_supported);
    }

    return {};
}

} // namespace os::sandbox
