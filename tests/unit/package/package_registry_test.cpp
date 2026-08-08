#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/package/registry.hpp>

namespace pkg = os::package;

namespace {

pkg::SignerLineageId signer(std::byte marker) {
    pkg::SignerLineageId value{};
    value.bytes[0] = marker;
    return value;
}

pkg::ContentDigest digest(std::byte marker) {
    pkg::ContentDigest value{};
    value.bytes[0] = marker;
    return value;
}

pkg::ApplicationIdentity application(const char* id, std::byte signer_marker) {
    auto package_id = pkg::PackageId::parse(id);
    assert(package_id);
    return pkg::ApplicationIdentity{
        .package_id = package_id.value(),
        .signer_lineage = signer(signer_marker),
    };
}

pkg::PackageGenerationRecord generation(
    const pkg::ApplicationIdentity& app,
    std::uint64_t generation_id,
    std::byte digest_marker) {
    return pkg::PackageGenerationRecord{
        .application = app,
        .generation = pkg::PackageGenerationId{generation_id},
        .content = digest(digest_marker),
    };
}

} // namespace

int main() {
    pkg::PackageRegistry registry;
    const auto notes = application("com.emnl.notes", std::byte{0x11});

    const auto generation1 = generation(notes, 1U, std::byte{0x21});
    assert(registry.stage_generation(generation1));
    assert(registry.application_count() == 1U);

    auto before_activation = registry.active(notes);
    assert(!before_activation);
    assert(before_activation.error().code == pkg::errors::no_active_generation);

    assert(registry.activate(notes, pkg::PackageGenerationId{1U}));
    auto active1 = registry.active(notes);
    assert(active1);
    assert(active1.value() == generation1);

    const auto generation2 = generation(notes, 2U, std::byte{0x22});
    assert(registry.stage_generation(generation2));
    assert(registry.active(notes).value().generation.value() == 1U);
    assert(registry.generation(notes, pkg::PackageGenerationId{1U}));
    assert(registry.generation(notes, pkg::PackageGenerationId{2U}));

    assert(registry.activate(notes, pkg::PackageGenerationId{2U}));
    assert(registry.active(notes).value().generation.value() == 2U);
    assert(registry.generation(notes, pkg::PackageGenerationId{1U}));

    const auto impostor = application("com.emnl.notes", std::byte{0x99});
    auto collision = registry.stage_generation(generation(impostor, 3U, std::byte{0x23}));
    assert(!collision);
    assert(collision.error().domain == os::core::ErrorDomain::package);
    assert(collision.error().code == pkg::errors::package_id_collision);

    auto duplicate = registry.stage_generation(generation(notes, 2U, std::byte{0x24}));
    assert(!duplicate);
    assert(duplicate.error().code == pkg::errors::generation_conflict);
    auto rollback = registry.stage_generation(generation(notes, 1U, std::byte{0x25}));
    assert(!rollback);
    assert(rollback.error().code == pkg::errors::generation_conflict);

    auto owner = registry.owner(notes.package_id);
    assert(owner);
    assert(owner.value() == notes);

    // Uninstall revokes future activation-based launch resolution but keeps
    // signer ownership and historical generations as a tombstone.
    assert(registry.uninstall(notes));
    auto uninstalled = registry.active(notes);
    assert(!uninstalled);
    assert(uninstalled.error().code == pkg::errors::no_active_generation);
    assert(registry.owner(notes.package_id).value() == notes);
    assert(registry.generation(notes, pkg::PackageGenerationId{1U}));
    assert(registry.generation(notes, pkg::PackageGenerationId{2U}));
    assert(registry.uninstall(notes)); // idempotent

    auto impostor_after_uninstall = registry.stage_generation(
        generation(impostor, 3U, std::byte{0x26}));
    assert(!impostor_after_uninstall);
    assert(impostor_after_uninstall.error().code == pkg::errors::package_id_collision);

    // Reinstall/update under the same signer continues monotonically rather
    // than resetting generation history.
    const auto generation3 = generation(notes, 3U, std::byte{0x27});
    assert(registry.stage_generation(generation3));
    assert(registry.activate(notes, pkg::PackageGenerationId{3U}));
    assert(registry.active(notes).value() == generation3);

    assert(registry.stage_generation(generation(notes, 4U, std::byte{0x28})));
    auto fifth = registry.stage_generation(generation(notes, 5U, std::byte{0x29}));
    assert(!fifth);
    assert(fifth.error().code == pkg::errors::generation_capacity);

    return 0;
}
