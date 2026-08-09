#pragma once

#include <os/core/result.hpp>
#include <os/keys/key.hpp>

namespace os::keys {

// Key providers own secret material. ENML core/service code receives only an
// opaque provider reference and never asks a provider to export a long-lived
// raw key. Hardware-backed TPM/TEE/HSM providers can implement this interface
// later without changing public application key identities.
class KeyProvider {
public:
    virtual ~KeyProvider() = default;

    [[nodiscard]] virtual os::core::Result<ProviderKeyReference>
    generate(KeyPurpose purpose) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    destroy(ProviderKeyReference key) noexcept = 0;
};

} // namespace os::keys
