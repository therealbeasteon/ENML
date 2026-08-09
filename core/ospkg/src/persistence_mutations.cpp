#include <os/package/persistence.hpp>

namespace os::package {

os::core::Result<void>
PersistentPackageRegistry::uninstall(const ApplicationIdentity& application) noexcept {
    PackageRegistry candidate = registry_;
    auto removed = candidate.uninstall(application);
    if (!removed) return removed.error();

    auto persisted = persist_candidate(candidate);
    if (!persisted) return persisted.error();
    registry_ = candidate;
    return {};
}

} // namespace os::package
