#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/ui/contrast.hpp>
#include <os/ui/error.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "contrast: %s\n", what);
    }
    return condition;
}

bool refused(const os::core::Result<void>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::ui &&
        result.error().code == code;
}

constexpr os::ui::Rgba8 rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return os::ui::Rgba8{r, g, b, 255U};
}

constexpr os::ui::Rgba8 white = rgb(255U, 255U, 255U);
constexpr os::ui::Rgba8 black = rgb(0U, 0U, 0U);

} // namespace

int main() {
    // The two anchors of the scale. Black against white is exactly 21.0:1, and
    // any colour against itself is exactly 1.0:1. Both are exact rather than
    // approximate because the table's endpoints are exact.
    if (!check(os::ui::contrast_ratio(black, white) == 210U,
               "black on white was not 21.0:1")) return 1;
    if (!check(os::ui::contrast_ratio(white, white) == 10U,
               "white on white was not 1.0:1")) return 1;
    if (!check(os::ui::contrast_ratio(black, black) == 10U,
               "black on black was not 1.0:1")) return 1;

    // Luminance endpoints, which the ratio above depends on.
    if (!check(os::ui::relative_luminance(black) == 0U, "black luminance was not zero")) return 1;
    if (!check(os::ui::relative_luminance(white) == 1'000'000U,
               "white luminance was not full scale")) return 1;

    // Symmetric: the ratio is defined with the lighter colour on top, so the
    // argument order must not matter.
    for (std::uint16_t step = 0U; step < 255U; step = static_cast<std::uint16_t>(step + 17U)) {
        const auto channel = static_cast<std::uint8_t>(step);
        const auto grey = rgb(channel, channel, channel);
        if (!check(os::ui::contrast_ratio(grey, white) == os::ui::contrast_ratio(white, grey),
                   "ratio was not symmetric")) return 1;
    }

    // Green contributes far more luminance than blue, so pure green is much
    // lighter than pure blue. A contrast implementation that averaged the
    // channels instead of weighting them would fail here.
    if (!check(os::ui::relative_luminance(rgb(0U, 255U, 0U)) >
                   os::ui::relative_luminance(rgb(0U, 0U, 255U)),
               "channel weighting is wrong")) return 1;
    if (!check(os::ui::relative_luminance(rgb(255U, 0U, 0U)) >
                   os::ui::relative_luminance(rgb(0U, 0U, 255U)),
               "red should outweigh blue")) return 1;

    // Monotonic: a lighter grey never has lower luminance than a darker one.
    for (std::uint16_t value = 1U; value < 256U; ++value) {
        const auto lower = os::ui::relative_luminance(
            rgb(static_cast<std::uint8_t>(value - 1U),
                static_cast<std::uint8_t>(value - 1U),
                static_cast<std::uint8_t>(value - 1U)));
        const auto upper = os::ui::relative_luminance(
            rgb(static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(value)));
        if (!check(upper > lower, "luminance was not strictly increasing")) return 1;
    }

    // Body text at the threshold. Mid grey on white is around 4.5:1, so it is
    // the useful place to check that the boundary is enforced rather than
    // approximated: one step darker passes, one step lighter fails.
    {
        std::uint8_t boundary = 0U;
        for (std::uint16_t value = 0U; value < 256U; ++value) {
            const auto grey = rgb(static_cast<std::uint8_t>(value),
                                  static_cast<std::uint8_t>(value),
                                  static_cast<std::uint8_t>(value));
            if (os::ui::contrast_ratio(grey, white) < os::ui::minimum_text_contrast_ratio) {
                boundary = static_cast<std::uint8_t>(value);
                break;
            }
        }
        if (!check(boundary != 0U, "no grey failed the body-text threshold")) return 1;

        const auto passing = rgb(static_cast<std::uint8_t>(boundary - 1U),
                                 static_cast<std::uint8_t>(boundary - 1U),
                                 static_cast<std::uint8_t>(boundary - 1U));
        const auto failing = rgb(boundary, boundary, boundary);
        if (!check(static_cast<bool>(
                       os::ui::validate_text_contrast(passing, white, 12U, false)),
                   "colour just inside the threshold was refused")) return 1;
        if (!check(refused(os::ui::validate_text_contrast(failing, white, 12U, false),
                           os::ui::errors::insufficient_text_contrast),
                   "colour just outside the threshold was accepted")) return 1;

        // The same colour is acceptable as large text, where the relaxed
        // threshold applies - provided it still clears 3:1.
        if (os::ui::contrast_ratio(failing, white) >= os::ui::minimum_large_text_contrast_ratio) {
            if (!check(static_cast<bool>(os::ui::validate_text_contrast(
                           failing, white, os::ui::large_text_points, false)),
                       "large text refused above the relaxed threshold")) return 1;
            // Bold qualifies as large at a smaller point size.
            if (!check(static_cast<bool>(os::ui::validate_text_contrast(
                           failing, white, os::ui::large_bold_text_points, true)),
                       "large bold text refused above the relaxed threshold")) return 1;
            // The same size unbolded does not qualify.
            if (!check(refused(os::ui::validate_text_contrast(
                                   failing, white, os::ui::large_bold_text_points, false),
                               os::ui::errors::insufficient_text_contrast),
                       "small non-bold text used the relaxed threshold")) return 1;
        }
    }

    // Text on its own background is invisible and must never pass, whatever
    // the size.
    if (!check(refused(os::ui::validate_text_contrast(white, white, 96U, true),
                       os::ui::errors::insufficient_text_contrast),
               "invisible text accepted at a large size")) return 1;

    // Translucency has no defined contrast until it is composited. Refusing is
    // the honest answer; returning a number would be inventing the backdrop.
    if (!check(refused(os::ui::validate_text_contrast(
                           os::ui::Rgba8{0U, 0U, 0U, 128U}, white, 12U, false),
                       os::ui::errors::indeterminate_contrast),
               "translucent foreground measured")) return 1;
    if (!check(refused(os::ui::validate_text_contrast(
                           black, os::ui::Rgba8{255U, 255U, 255U, 0U}, 12U, false),
                       os::ui::errors::indeterminate_contrast),
               "translucent background measured")) return 1;
    if (!check(refused(os::ui::validate_text_contrast(black, white, 0U, false),
                       os::ui::errors::indeterminate_contrast),
               "zero point size measured")) return 1;

    return 0;
}
