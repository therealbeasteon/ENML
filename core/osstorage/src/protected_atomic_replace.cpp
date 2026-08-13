#include <os/storage/protected_atomic_replace.hpp>

#include <os/storage/error.hpp>

namespace os::storage {

os::core::Result<void>
ProtectedAtomicReplace::replace(
    const ProtectedObjectVersion& previous,
    const ProtectedObjectVersion& next,
    os::core::ByteSpan plaintext) noexcept {
    if (backend_ == nullptr || !previous.valid() || !next.valid() ||
        previous.user != next.user || previous.object_id != next.object_id ||
        previous.generation == next.generation || plaintext.size() > protected_chunk_plaintext_bytes) {
        return storage_error(errors::invalid_options);
    }
    if (next.generation <= previous.generation) {
        return storage_error(errors::invalid_options);
    }

    ProtectedPublication publication{};

    auto result = backend_->persist_wrapped_key(next);
    if (!result) return result.error();
    result = publication.mark_key_durable();
    if (!result) return result.error();

    result = backend_->persist_ciphertext(next, plaintext);
    if (!result) return result.error();
    result = publication.mark_ciphertext_durable();
    if (!result) return result.error();

    result = backend_->persist_commit_record(previous, next);
    if (!result) return result.error();
    result = publication.mark_commit_record_durable();
    if (!result) return result.error();

    result = backend_->publish_namespace(next);
    if (!result) return result.error();
    result = publication.mark_namespace_published();
    if (!result) return result.error();

    result = backend_->retire_generation(previous);
    if (!result) return result.error();
    return publication.mark_retired();
}

} // namespace os::storage
