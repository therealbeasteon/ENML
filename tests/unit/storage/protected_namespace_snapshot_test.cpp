#include <cassert>

#include <os/storage/protected_namespace_snapshot.hpp>

int main() {
    const os::storage::ProtectedNamespaceSnapshotHeaderV1 header{
        .user = os::core::UserId{42U},
        .security_epoch = os::keys::SecurityEpoch{9U},
        .sequence = 7U,
        .entry_count = 3U,
        .flags = 0U,
    };
    const os::storage::ProtectedNamespaceFreshnessEvidence current{
        .user = os::core::UserId{42U},
        .current_security_epoch = os::keys::SecurityEpoch{9U},
        .minimum_sequence = 7U,
    };
    assert(os::storage::validate_namespace_snapshot_freshness(header, current));

    auto stale_epoch = current;
    stale_epoch.current_security_epoch = os::keys::SecurityEpoch{10U};
    assert(!os::storage::validate_namespace_snapshot_freshness(header, stale_epoch));

    auto stale_sequence = current;
    stale_sequence.minimum_sequence = 8U;
    assert(!os::storage::validate_namespace_snapshot_freshness(header, stale_sequence));

    auto wrong_user = current;
    wrong_user.user = os::core::UserId{43U};
    assert(!os::storage::validate_namespace_snapshot_freshness(header, wrong_user));

    auto future_epoch_record = header;
    future_epoch_record.security_epoch = os::keys::SecurityEpoch{10U};
    assert(!os::storage::validate_namespace_snapshot_freshness(future_epoch_record, current));
    return 0;
}
