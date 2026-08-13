#include <os/supervisor/subsystem_lease_authority.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::supervisor;
    const os::core::PrincipalId app{.high = 1U, .low = 2U};
    const os::core::PrincipalId other{.high = 3U, .low = 4U};

    SubsystemLeaseAuthority camera{SubsystemDomain::camera};
    auto first = camera.acquire(app, LeaseKind::interactive);
    require(first.has_value());
    require(camera.active(*first));
    require(camera.counts().interactive == 1U);

    // Possession without the owning principal is insufficient to release.
    require(!camera.release(other, *first));
    require(camera.active(*first));
    require(camera.release(app, *first));
    require(camera.counts().total() == 0U);

    auto second = camera.acquire(app, LeaseKind::background);
    require(second.has_value());
    const auto old_generation = second->generation;
    camera.advance_generation_after_quiesce();
    require(camera.generation() != old_generation);
    require(!camera.active(*second));
    require(!camera.release(app, *second));

    auto fresh = camera.acquire(app, LeaseKind::interactive);
    require(fresh.has_value());
    require(fresh->generation == camera.generation());
    require(fresh->generation != old_generation);

    // Cross-domain token confusion fails closed.
    SubsystemLeaseAuthority network{SubsystemDomain::network};
    require(!network.active(*fresh));

    return 0;
}
