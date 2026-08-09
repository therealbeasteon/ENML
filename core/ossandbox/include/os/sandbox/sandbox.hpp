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
    bool require_landlock {false};

    std::uint32_t max_open_files {32};
    std::uint32_t max_processes {8};
    std::uint64_t max_file_size_bytes {1024U * 1024U};
};

// Borrowed Linux-private descriptors used only while constructing an
// application sandbox. executable_fd must name the exact immutable executable
// selected by App Manager. private_data_directory_fd is optional: -1 means the
// application receives no direct writable filesystem tree. M2.2 product apps
// use that mode and reach private data only through Storage Service objects.
struct ApplicationSandboxHandlesV1 final {
    int executable_fd {-1};
    int private_data_directory_fd {-1};
};

// Applies the service restriction profile immediately before execve().
[[nodiscard]] os::core::Result<void>
apply_before_exec(const char* executable_path, const SandboxPolicyV1& policy) noexcept;

// Applies the application restriction profile using already-authorized file
// descriptors. When a private-data fd is present, it is writable but never
// executable. When it is -1, Landlock grants no direct private-data tree.
// Linux paths remain private implementation details.
[[nodiscard]] os::core::Result<void>
apply_application_before_exec(
    const ApplicationSandboxHandlesV1& handles,
    const SandboxPolicyV1& policy) noexcept;

} // namespace os::sandbox
