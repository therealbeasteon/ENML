#pragma once

#include <cstdint>

#include <os/core/result.hpp>

namespace os::sandbox {

// M0.8 Linux service sandbox. These are implementation mechanisms, not a
// public application ABI. The profile intentionally starts small and bounded.
struct SandboxPolicyV1 final {
    bool enabled {true};
    bool require_no_new_privs {true};
    bool clear_capabilities {true};
    bool require_seccomp {true};
    // Optional in M0.8: some container/CI kernels expose Linux >=5.13 but
    // block the Landlock syscalls. Full data-caging is not allowed to depend
    // on this optional mechanism alone.
    bool require_landlock {false};

    std::uint32_t max_open_files {32};
    std::uint32_t max_processes {8};
    std::uint64_t max_file_size_bytes {1024U * 1024U};
};

// Applies restrictions in the forked child immediately before execve().
// executable_path is admitted through the filesystem policy; normal write
// access remains denied. This function is Linux-private implementation code.
[[nodiscard]] os::core::Result<void>
apply_before_exec(const char* executable_path, const SandboxPolicyV1& policy) noexcept;

} // namespace os::sandbox
