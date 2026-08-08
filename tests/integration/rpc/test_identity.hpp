#pragma once

#include <os/core/error.hpp>
#include <os/core/identity.hpp>
#include <os/ipc/rpc.hpp>

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    TestIdentityResolver(
        std::int64_t native_pid,
        std::uint32_t native_uid,
        std::uint32_t native_gid,
        os::core::PeerIdentity identity) noexcept
        : credentials_{native_pid, native_uid, native_gid}, identity_(identity) {}

    [[nodiscard]] os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials != credentials_) {
            return os::core::make_error(
                os::core::ErrorDomain::security,
                os::core::errors::security::unknown_process);
        }
        return identity_;
    }

private:
    os::ipc::KernelPeerCredentials credentials_ {};
    os::core::PeerIdentity identity_ {};
};
