#include <os/network/policy.hpp>

namespace os::network {

// The first networking slice is intentionally policy-only and contains no host
// socket implementation. Keeping a translation unit gives the module a concrete
// library boundary now while the future Cookie-native transport/driver services
// consume the same hardware-neutral policy.
static_assert(plan_transport(LinkObservation{}, PrivacyMode::zero_tracking)
                  .require_encrypted_transport);

} // namespace os::network
