#include <os/keys/persistence.hpp>

#include <os/core/result.hpp>
#include <os/keys/error.hpp>

namespace os::keys {

os::core::Result<KeyDescriptor>
PersistentKeyRegistry::adopt_generated(
    KeyOwner owner,
    KeyId id,
    KeyPurpose purpose,
    RightsMask rights,
    ProviderKeyReference provider_key) noexcept {
    if (provider_ == nullptr || !provider_key.valid()) {
        return key_error(errors::provider_failure);
    }

    KeyRegistry candidate = registry_;
    auto adopted = candidate.adopt_generated(owner, id, purpose, rights, provider_key);
    if (!adopted) {
        os::core::discard_result(provider_->destroy(provider_key));
        return adopted.error();
    }

    bool replaced = false;
    auto persisted = persist_candidate(candidate, replaced);
    if (!persisted) {
        if (replaced) {
            // The replacement snapshot already names this provider object. Keep
            // memory consistent with visible durable state and report only the
            // late durability error (for example, directory fsync failure).
            registry_ = candidate;
        } else {
            os::core::discard_result(provider_->destroy(provider_key));
        }
        return persisted.error();
    }

    registry_ = candidate;
    return adopted.value();
}

} // namespace os::keys
