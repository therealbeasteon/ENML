#include <os/ui/contrast.hpp>

#include <array>
#include <cstddef>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>

namespace os::ui {
namespace {

// Luminance is accumulated at this scale. One million keeps the whole range in
// a 32-bit value while leaving enough resolution that adjacent channel values
// remain distinguishable at the dark end, where the curve is steepest.
inline constexpr std::uint64_t luminance_scale = 1'000'000U;

// The sRGB electro-optical transfer function, evaluated once per possible
// channel value. Entry i is the linearised value of i/255, scaled by
// luminance_scale: i/255 divided by 12.92 below the knee at 0.04045, and
// ((i/255 + 0.055) / 1.055) raised to 2.4 above it.
//
// Tabulated rather than computed so the result is identical on every target and
// needs no math library. The two ends are exact by construction - entry 0 is
// zero and entry 255 is the scale - which is what makes black against white
// come out at precisely 21.0:1 rather than a rounding of it.
inline constexpr std::array<std::uint32_t, 256> linear_channel{
          0,     304,     607,     911,    1214,    1518,    1821,    2125,
       2428,    2732,    3035,    3347,    3677,    4025,    4391,    4777,
       5182,    5605,    6049,    6512,    6995,    7499,    8023,    8568,
       9134,    9721,   10330,   10960,   11612,   12286,   12983,   13702,
      14444,   15209,   15996,   16807,   17642,   18500,   19382,   20289,
      21219,   22174,   23153,   24158,   25187,   26241,   27321,   28426,
      29557,   30713,   31896,   33105,   34340,   35601,   36889,   38204,
      39546,   40915,   42311,   43735,   45186,   46665,   48172,   49707,
      51269,   52861,   54480,   56128,   57805,   59511,   61246,   63010,
      64803,   66626,   68478,   70360,   72272,   74214,   76185,   78187,
      80220,   82283,   84376,   86500,   88656,   90842,   93059,   95307,
      97587,   99899,  102242,  104616,  107023,  109462,  111932,  114435,
     116971,  119538,  122139,  124772,  127438,  130136,  132868,  135633,
     138432,  141263,  144128,  147027,  149960,  152926,  155926,  158961,
     162029,  165132,  168269,  171441,  174647,  177888,  181164,  184475,
     187821,  191202,  194618,  198069,  201556,  205079,  208637,  212231,
     215861,  219526,  223228,  226966,  230740,  234551,  238398,  242281,
     246201,  250158,  254152,  258183,  262251,  266356,  270498,  274677,
     278894,  283149,  287441,  291771,  296138,  300544,  304987,  309469,
     313989,  318547,  323143,  327778,  332452,  337164,  341914,  346704,
     351533,  356400,  361307,  366253,  371238,  376262,  381326,  386429,
     391572,  396755,  401978,  407240,  412543,  417885,  423268,  428690,
     434154,  439657,  445201,  450786,  456411,  462077,  467784,  473531,
     479320,  485150,  491021,  496933,  502886,  508881,  514918,  520996,
     527115,  533276,  539479,  545724,  552011,  558340,  564712,  571125,
     577580,  584078,  590619,  597202,  603827,  610496,  617207,  623960,
     630757,  637597,  644480,  651406,  658375,  665387,  672443,  679542,
     686685,  693872,  701102,  708376,  715694,  723055,  730461,  737910,
     745404,  752942,  760525,  768151,  775822,  783538,  791298,  799103,
     806952,  814847,  822786,  830770,  838799,  846873,  854993,  863157,
     871367,  879622,  887923,  896269,  904661,  913099,  921582,  930111,
     938686,  947307,  955973,  964686,  973445,  982251,  991102, 1000000,
};

// The 0.05 offset the ratio is defined with, at luminance_scale.
inline constexpr std::uint64_t luminance_offset = luminance_scale / 20U;

} // namespace

std::uint32_t relative_luminance(Rgba8 color) noexcept {
    // Coefficients are the standard luminous contribution of each primary,
    // carried as ten-thousandths so the weighting is exact.
    const std::uint64_t weighted =
        (2126ULL * linear_channel[color.red]) +
        (7152ULL * linear_channel[color.green]) +
        (722ULL * linear_channel[color.blue]);
    return static_cast<std::uint32_t>(weighted / 10'000ULL);
}

std::uint32_t contrast_ratio(Rgba8 first, Rgba8 second) noexcept {
    const std::uint64_t a = static_cast<std::uint64_t>(relative_luminance(first)) +
        luminance_offset;
    const std::uint64_t b = static_cast<std::uint64_t>(relative_luminance(second)) +
        luminance_offset;
    const std::uint64_t lighter = a > b ? a : b;
    const std::uint64_t darker = a > b ? b : a;

    // darker is at least the offset, so this cannot divide by zero.
    return static_cast<std::uint32_t>((lighter * contrast_ratio_scale) / darker);
}

os::core::Result<void> validate_text_contrast(
    Rgba8 foreground,
    Rgba8 background,
    std::uint32_t point_size,
    bool bold) noexcept {
    // A translucent colour has no contrast until it is composited. Answering
    // anyway would mean inventing what is behind it and returning the invention
    // as a measurement.
    if (foreground.alpha != 255U || background.alpha != 255U) {
        return ui_error(errors::indeterminate_contrast);
    }
    if (point_size == 0U) {
        return ui_error(errors::indeterminate_contrast);
    }

    const auto threshold_size = bold ? large_bold_text_points : large_text_points;
    const auto required = point_size >= threshold_size
        ? minimum_large_text_contrast_ratio
        : minimum_text_contrast_ratio;

    if (contrast_ratio(foreground, background) < required) {
        return ui_error(errors::insufficient_text_contrast);
    }
    return {};
}

} // namespace os::ui
