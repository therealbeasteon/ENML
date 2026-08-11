#include <os/shell/consent.hpp>

#include <os/core/error.hpp>
#include <os/shell/error.hpp>
#include <os/ui/contrast.hpp>

namespace os::shell {

bool ConsentGrant::authorizes(
    os::core::PeerIdentity principal,
    ConsentScope scope) const noexcept {
    // Gated on granted_ first so a refusal cannot authorise anything even if
    // its subject and scope happen to match what a caller asks about.
    return granted_ && subject_ == principal && scope_ == scope;
}

os::core::Result<ConsentPrompt> ConsentPrompt::present(
    const ConsentRequest& request,
    const ConsentPresentation& presentation,
    std::uint64_t prompt_id) noexcept {
    using ResultType = os::core::Result<ConsentPrompt>;

    if (!request.valid()) {
        return ResultType{shell_error(errors::invalid_consent_request)};
    }
    // Zero is reserved for "no prompt", so an answer carrying it can never
    // match a live question.
    if (prompt_id == 0U) {
        return ResultType{shell_error(errors::invalid_consent_request)};
    }

    // A prompt that grants authority while drawn as an ordinary application
    // surface is a prompt an application can draw. The trusted presentation is
    // compositor-attributed and cannot be claimed by a client.
    if (presentation.presentation != os::display::TrustedPresentation::secure_system) {
        return ResultType{shell_error(errors::untrusted_consent_presentation)};
    }

    // The question must be answerable. These are the checks that were
    // previously available for a caller to remember to use; here they are the
    // condition of the prompt existing at all.
    auto layout = os::ui::validate_decision_layout(presentation.choices);
    if (!layout) return ResultType{layout.error()};

    auto legible = os::ui::validate_text_contrast(
        presentation.text,
        presentation.background,
        presentation.point_size,
        presentation.bold);
    if (!legible) return ResultType{legible.error()};

    ConsentPrompt prompt{};
    prompt.request_ = request;
    prompt.prompt_id_ = prompt_id;
    return ResultType{prompt};
}

os::core::Result<ConsentGrant> ConsentPrompt::resolve(
    os::ui::ChoiceKind answer,
    std::uint64_t answered_prompt_id) noexcept {
    using ResultType = os::core::Result<ConsentGrant>;

    // An answer that arrived for a different question is refused rather than
    // applied to this one - the same discipline the compositor applies to an
    // input hit taken against a frame that is no longer presented.
    if (answered_prompt_id != prompt_id_) {
        return ResultType{shell_error(errors::stale_consent_answer)};
    }
    // Consent is an event. A prompt that can be answered twice is a permission
    // the user was never asked for a second time.
    if (answered_) {
        return ResultType{shell_error(errors::consent_already_answered)};
    }
    // A neutral choice does not resolve the question, so it must not close it.
    // Leaving the prompt unanswered is what lets the user read more and then
    // still decide.
    if (answer != os::ui::ChoiceKind::affirmative && answer != os::ui::ChoiceKind::negative) {
        return ResultType{shell_error(errors::invalid_consent_answer)};
    }

    answered_ = true;

    ConsentGrant grant{};
    grant.prompt_id_ = prompt_id_;
    if (answer == os::ui::ChoiceKind::affirmative) {
        // Bound to the attested requester and to the one scope that was asked
        // about. Nothing here is copied from anything the requester supplied.
        grant.granted_ = true;
        grant.subject_ = request_.requester;
        grant.scope_ = request_.scope;
    }
    return ResultType{grant};
}

} // namespace os::shell
