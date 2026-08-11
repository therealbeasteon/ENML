#pragma once

#include <cstdint>
#include <span>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/display/types.hpp>
#include <os/ui/decision.hpp>
#include <os/ui/raster.hpp>

// Asking the user to grant authority, in a way the answer can be trusted.
//
// The passkey flows in the references get the essential property right, and it
// is worth naming because it is the one usually missed: the prompt says which
// party is being authorised - "sign in to bestbuy.com" - and the credential it
// releases is bound to that party and usable by nobody else. The phishing
// resistance is not in the cryptography, which would work identically without
// it; it is in the binding between what the user was told and what the answer
// authorises.
//
// Everything here follows from taking that seriously:
//
//   - The requester is never allowed to say who it is. There is no display-name
//     field on a request, because a field an attacker fills is a field that
//     will eventually read "System". The subject comes from the attested peer
//     identity the broker already established, and the shell renders it.
//
//   - A grant names the principal it was made to and the single scope it was
//     made for, and authorises nothing else. A grant obtained for one purpose
//     cannot be presented for another, and a grant obtained by one application
//     cannot be used by a second.
//
//   - A grant answers exactly once. Consent is an event, not a standing
//     property; a grant that can be replayed is a permission the user was never
//     asked for a second time.
//
//   - A prompt cannot be presented at all unless its layout and its text pass
//     the checks that make the question legible and the answer meaningful. The
//     validators for those already exist and were, until now, available for a
//     caller to remember to use.
//
// Default-constructed values authorise nothing, as everywhere else in the tree.
namespace os::shell {

// Zero is never a valid scope. An all-zero request must not decode to a
// meaningful permission.
enum class ConsentScope : std::uint32_t {
    private_data_read = 1U,
    private_data_write = 2U,
    location = 3U,
    camera = 4U,
    microphone = 5U,
    contacts = 6U,
};

[[nodiscard]] constexpr bool valid_consent_scope(ConsentScope scope) noexcept {
    return scope >= ConsentScope::private_data_read && scope <= ConsentScope::contacts;
}

// What the shell was asked to ask.
//
// Deliberately has no field for how the requester wishes to be described. The
// shell derives the subject from `requester`, which the broker attested.
struct ConsentRequest final {
    os::core::PeerIdentity requester {};
    ConsentScope scope {};

    [[nodiscard]] bool valid() const noexcept {
        return os::core::valid_peer_identity(requester) && valid_consent_scope(scope);
    }
};

// How the question will be presented. Checked before the prompt may exist.
struct ConsentPresentation final {
    std::span<const os::ui::DecisionChoice> choices {};
    os::ui::Rgba8 text {};
    os::ui::Rgba8 background {};
    std::uint32_t point_size {0U};
    bool bold {false};
    // Must be secure_system presentation. A prompt that grants authority while
    // drawn as an ordinary application surface is one an application can draw.
    os::display::TrustedPresentation presentation {os::display::TrustedPresentation::none};
};

// The user's answer, bound to who asked and what for.
//
// A default grant authorises nothing: not because its fields are empty, but
// because `granted_` is false and every accessor is gated on it.
class ConsentGrant final {
public:
    ConsentGrant() noexcept = default;

    // The only question callers should ask. Both the principal and the scope
    // must match; a grant is not a token that means "the user said yes to
    // something".
    [[nodiscard]] bool authorizes(
        os::core::PeerIdentity principal,
        ConsentScope scope) const noexcept;

    [[nodiscard]] bool granted() const noexcept { return granted_; }
    [[nodiscard]] ConsentScope scope() const noexcept { return scope_; }
    [[nodiscard]] std::uint64_t prompt_id() const noexcept { return prompt_id_; }

private:
    friend class ConsentPrompt;

    bool granted_ {false};
    os::core::PeerIdentity subject_ {};
    ConsentScope scope_ {};
    std::uint64_t prompt_id_ {0U};
};

// A question that has been validated and may be put to the user.
//
// Construction is the check. A ConsentPrompt that exists is one whose layout,
// contrast and presentation were all acceptable, so a caller cannot hold an
// unvalidated prompt and present it anyway.
class ConsentPrompt final {
public:
    // prompt_id must be unique for the lifetime of the shell's prompt sequence
    // and nonzero. It is what the answer is matched against, so that an answer
    // to one question cannot be delivered as the answer to another.
    [[nodiscard]] static os::core::Result<ConsentPrompt> present(
        const ConsentRequest& request,
        const ConsentPresentation& presentation,
        std::uint64_t prompt_id) noexcept;

    [[nodiscard]] const ConsentRequest& request() const noexcept { return request_; }
    [[nodiscard]] std::uint64_t prompt_id() const noexcept { return prompt_id_; }
    [[nodiscard]] bool answered() const noexcept { return answered_; }

    // Records the user's answer and returns the resulting grant.
    //
    // `answered_prompt_id` is the id the input path believed it was answering.
    // It must equal this prompt's id: an answer that arrived for a different
    // question is refused rather than applied to this one, which is the same
    // discipline the compositor applies to a stale input hit.
    //
    // Answering twice is refused. A refusal produces a grant that authorises
    // nothing rather than an error, because "the user said no" is an answer and
    // not a fault.
    [[nodiscard]] os::core::Result<ConsentGrant> resolve(
        os::ui::ChoiceKind answer,
        std::uint64_t answered_prompt_id) noexcept;

private:
    ConsentPrompt() noexcept = default;

    ConsentRequest request_ {};
    std::uint64_t prompt_id_ {0U};
    bool answered_ {false};
};

} // namespace os::shell
