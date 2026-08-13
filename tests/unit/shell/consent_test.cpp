#include <array>
#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/shell/consent.hpp>
#include <os/shell/error.hpp>
#include <os/ui/error.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "consent: %s\n", what);
    }
    return condition;
}

constexpr os::core::PeerIdentity requester{
    os::core::PrincipalId{0x4150500000000201ULL, 2U},
    os::core::UserId{7U},
    os::core::ProcessId{202U},
};
constexpr os::core::PeerIdentity impostor{
    os::core::PrincipalId{0x4150500000000202ULL, 2U},
    os::core::UserId{7U},
    os::core::ProcessId{303U},
};

constexpr std::uint32_t target = os::ui::minimum_touch_target_q6;
constexpr std::uint32_t gap = os::ui::minimum_target_separation_q6;

constexpr std::array choices{
    os::ui::DecisionChoice{
        os::ui::ChoiceKind::negative,
        os::ui::LogicalRect{0, 0, target, target}},
    os::ui::DecisionChoice{
        os::ui::ChoiceKind::affirmative,
        os::ui::LogicalRect{static_cast<std::int32_t>(target + gap), 0, target, target}},
};

os::shell::ConsentPresentation good_presentation() {
    return os::shell::ConsentPresentation{
        .choices = choices,
        .text = os::ui::Rgba8{0U, 0U, 0U, 255U},
        .background = os::ui::Rgba8{255U, 255U, 255U, 255U},
        .point_size = 14U,
        .bold = false,
        .presentation = os::display::TrustedPresentation::secure_system,
    };
}

constexpr os::shell::ConsentRequest request{requester, os::shell::ConsentScope::camera};

} // namespace

int main() {
    // A default grant authorises nothing, including for the principal and
    // scope it would have named.
    {
        const os::shell::ConsentGrant defaulted{};
        if (!check(!defaulted.granted(), "default grant reported granted")) return 1;
        if (!check(!defaulted.authorizes(requester, os::shell::ConsentScope::camera),
                   "default grant authorised")) return 1;
    }

    // A prompt that grants authority must be presented as secure system UI. An
    // application-drawn prompt is one an application can forge.
    {
        auto untrusted = good_presentation();
        untrusted.presentation = os::display::TrustedPresentation::none;
        auto refused = os::shell::ConsentPrompt::present(request, untrusted, 1U);
        if (!check(!refused, "untrusted presentation accepted")) return 1;
        if (!check(refused.error().code == os::shell::errors::untrusted_consent_presentation,
                   "wrong error for untrusted presentation")) return 1;
    }

    // The layout and contrast checks are the condition of the prompt existing,
    // not something a caller may skip. A hairline refuse beside a large accept
    // is refused here, at the shell, rather than being drawn.
    {
        constexpr std::array lopsided{
            os::ui::DecisionChoice{
                os::ui::ChoiceKind::negative,
                os::ui::LogicalRect{0, 0, target, target}},
            os::ui::DecisionChoice{
                os::ui::ChoiceKind::affirmative,
                os::ui::LogicalRect{
                    static_cast<std::int32_t>(target + gap), 0, target * 4U, target}},
        };
        auto skewed = good_presentation();
        skewed.choices = lopsided;
        auto refused = os::shell::ConsentPrompt::present(request, skewed, 1U);
        if (!check(!refused, "disproportionate consent layout accepted")) return 1;
        if (!check(refused.error().code == os::ui::errors::disproportionate_decision_choice,
                   "layout error not surfaced")) return 1;
    }
    {
        auto faint = good_presentation();
        faint.text = os::ui::Rgba8{200U, 200U, 200U, 255U};
        auto refused = os::shell::ConsentPrompt::present(request, faint, 1U);
        if (!check(!refused, "illegible consent text accepted")) return 1;
        if (!check(refused.error().code == os::ui::errors::insufficient_text_contrast,
                   "contrast error not surfaced")) return 1;
    }

    // A prompt with no identifiable requester cannot name a subject, so it
    // cannot be asked at all.
    {
        const os::shell::ConsentRequest anonymous{
            os::core::PeerIdentity{}, os::shell::ConsentScope::camera};
        if (!check(!os::shell::ConsentPrompt::present(anonymous, good_presentation(), 1U),
                   "request without an attested requester accepted")) return 1;
    }
    // Zero is reserved so that an answer carrying it never matches a question.
    if (!check(!os::shell::ConsentPrompt::present(request, good_presentation(), 0U),
               "zero prompt id accepted")) return 1;

    // A granted answer binds to the attested requester and the single scope
    // that was asked about.
    {
        auto presented = os::shell::ConsentPrompt::present(request, good_presentation(), 42U);
        if (!check(static_cast<bool>(presented), "valid prompt refused")) return 1;
        auto prompt = presented.value();

        // An answer for a different question must not be applied to this one.
        if (!check(!prompt.resolve(os::ui::ChoiceKind::affirmative, 43U),
                   "answer to another prompt accepted")) return 1;
        if (!check(!prompt.answered(), "refused answer marked the prompt answered")) return 1;

        // A neutral choice does not resolve the question and must not close it.
        if (!check(!prompt.resolve(os::ui::ChoiceKind::neutral, 42U),
                   "neutral answer resolved the prompt")) return 1;
        if (!check(!prompt.answered(), "neutral answer closed the prompt")) return 1;

        auto resolved = prompt.resolve(os::ui::ChoiceKind::affirmative, 42U);
        if (!check(static_cast<bool>(resolved), "valid answer refused")) return 1;
        const auto grant = resolved.value();

        if (!check(grant.authorizes(requester, os::shell::ConsentScope::camera),
                   "grant did not authorise what was asked")) return 1;
        // Bound to the principal: another application cannot present it.
        if (!check(!grant.authorizes(impostor, os::shell::ConsentScope::camera),
                   "grant authorised a different principal")) return 1;
        // Bound to the scope: consent to one thing is not consent to another.
        if (!check(!grant.authorizes(requester, os::shell::ConsentScope::microphone),
                   "grant authorised a different scope")) return 1;

        // Consent is an event, not a standing property.
        if (!check(!prompt.resolve(os::ui::ChoiceKind::affirmative, 42U),
                   "prompt answered twice")) return 1;
    }

    // Refusal is an answer, not a fault, and the grant it produces authorises
    // nothing.
    {
        auto presented = os::shell::ConsentPrompt::present(request, good_presentation(), 7U);
        if (!check(static_cast<bool>(presented), "valid prompt refused")) return 1;
        auto prompt = presented.value();
        auto resolved = prompt.resolve(os::ui::ChoiceKind::negative, 7U);
        if (!check(static_cast<bool>(resolved), "refusal reported as an error")) return 1;
        if (!check(!resolved.value().granted(), "refusal produced a granted result")) return 1;
        if (!check(!resolved.value().authorizes(requester, os::shell::ConsentScope::camera),
                   "refusal authorised the request")) return 1;
    }

    return 0;
}
