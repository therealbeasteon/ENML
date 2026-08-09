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
// selected by App Manager. In legacy mode private_data_directory_fd is the
// authorized per-app/per-user writable data root. In brokered_storage mode the
// application receives a Storage Service endpoint instead and the private root
// is deliberately absent from its filesystem authority.
struct ApplicationSandboxHandlesV1 final {
    int executable_fd {-1};
    int private_data_directory_fd {-1};
    bool brokered_storage {false};
};

// Applies the service restriction profile immediately before execve().
[[nodiscard]] os::core::Result<void>
apply_before_exec(const char* executable_path, const SandboxPolicyV1& policy) noexcept;

// Applies the application restriction profile using already-authorized file
// descriptors. When Landlock is required, executable/runtime material is
// read-only and executable. Legacy private storage is writable but never
// executable; brokered storage grants no direct private-data filesystem rule.
[[nodiscard]] os::core::Result<void>
apply_application_before_exec(
    const ApplicationSandboxHandlesV1& handles,
    const SandboxPolicyV1& policy) noexcept;

} // namespace os::sandbox
