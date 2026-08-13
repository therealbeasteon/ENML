#include <cassert>
#include <cstdint>

#include <os/storage/protected_namespace.hpp>

namespace {

class TestIds final : public os::storage::ProtectedObjectIdSource {
public:
    os::core::Result<os::storage::ProtectedObjectId> next() noexcept override {
        return os::storage::ProtectedObjectId{0xAA00U + counter_, 0xBB00U + counter_++};
    }
private:
    std::uint64_t counter_{1U};
};

} // namespace

int main() {
    TestIds ids;
    os::storage::ProtectedNamespaceRegistry registry{ids};
    auto path = os::storage::RelativePath::parse("private/state.bin");
    assert(path);

    const os::core::PrincipalId principal{0x1111222233334444ULL, 0x5555666677778888ULL};
    const os::core::UserId user{42U};

    auto created = registry.create(principal, user, path.value());
    assert(created);
    assert(created.value().generation == 1U);
    assert(created.value().object_id.valid());
    assert(registry.size() == 1U);

    auto proposed = registry.propose_next_generation(principal, user, path.value(), 1U);
    assert(proposed);
    assert(proposed.value().generation == 2U);

    // Merely preparing a replacement must not mutate authoritative namespace
    // state. Failed staging therefore leaves generation 1 readable/recoverable.
    auto current = registry.find(principal, user, path.value());
    assert(current != nullptr);
    assert(current->generation == 1U);
    assert(current->object_id == created.value().object_id);

    assert(registry.publish_generation(principal, user, path.value(), 1U, 2U));
    current = registry.find(principal, user, path.value());
    assert(current != nullptr);
    assert(current->generation == 2U);
    assert(current->object_id == created.value().object_id);

    // Stale or skipping publishers cannot overwrite the trusted generation.
    assert(!registry.publish_generation(principal, user, path.value(), 1U, 3U));
    assert(!registry.propose_next_generation(principal, user, path.value(), 1U));
    assert(current->generation == 2U);

    assert(!registry.create(principal, user, path.value()));
    return 0;
}
