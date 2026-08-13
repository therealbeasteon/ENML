#include <os/network/resolution.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    ResolutionAuthority authority{};
    const os::core::PeerIdentity app{
        .principal = os::core::PrincipalId{0xAAU, 0xBBU},
        .user = os::core::UserId{2U},
        .process = os::core::ProcessId{4U},
    };
    const os::core::PeerIdentity other{
        .principal = os::core::PrincipalId{0xCCU, 0xDDU},
        .user = os::core::UserId{2U},
        .process = os::core::ProcessId{5U},
    };

    auto denied = authority.issue(app, false, PrivacyMode::zero_tracking);
    require(!denied.allowed && denied.refusal == ResolutionRefusal::permission_denied);

    const auto issued = authority.issue(app, true, PrivacyMode::zero_tracking);
    require(issued.allowed);
    require(issued.grant.valid());
    require(issued.grant.privacy == PrivacyMode::zero_tracking);

    // The resolver-visible grant intentionally contains no principal/user/process.
    require(authority.consume(other.principal, issued.grant) == ResolutionRefusal::wrong_principal);
    require(authority.consume(app.principal, issued.grant) == ResolutionRefusal::none);
    // Resolution grants are one-shot to avoid persistent tracking handles.
    require(authority.consume(app.principal, issued.grant) == ResolutionRefusal::stale_grant);

    const auto restart_grant = authority.issue(app, true, PrivacyMode::private_relay);
    require(restart_grant.allowed);
    authority.revoke_all_for_restart();
    require(authority.consume(app.principal, restart_grant.grant) == ResolutionRefusal::stale_grant);

    return 0;
}
