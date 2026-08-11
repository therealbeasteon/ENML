#include <os/storage/protected_publication.hpp>

#include <cassert>

int main() {
    using os::storage::ProtectedPublication;
    using os::storage::ProtectedPublicationPhase;

    ProtectedPublication publication;
    assert(publication.phase() == ProtectedPublicationPhase::idle);
    assert(publication.may_discard_staging_after_crash());
    assert(!publication.requires_recovery_after_crash());

    assert(publication.mark_key_durable());
    assert(publication.may_discard_staging_after_crash());
    assert(!publication.mark_commit_record_durable());

    assert(publication.mark_ciphertext_durable());
    assert(publication.may_discard_staging_after_crash());

    assert(publication.mark_commit_record_durable());
    assert(!publication.may_discard_staging_after_crash());
    assert(publication.requires_recovery_after_crash());
    assert(!publication.mark_retired());

    assert(publication.mark_namespace_published());
    assert(publication.requires_recovery_after_crash());

    assert(publication.mark_retired());
    assert(publication.phase() == ProtectedPublicationPhase::retired);
    assert(!publication.requires_recovery_after_crash());
    assert(!publication.mark_key_durable());

    return 0;
}
