#include <os/display/trusted_mark.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/display/error.hpp>

namespace {

void expect_display_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

os::display::TrustedOverlayEntry overlay_entry(
    std::uint32_t serial,
    os::display::TrustedPresentation presentation,
    os::display::Rect bounds) {
    return os::display::TrustedOverlayEntry{
        .surface = os::display::SurfaceId{
            os::display::make_display_object_value(1U, serial)},
        .presentation = presentation,
        .bounds = bounds,
        .frame_sequence = serial,
    };
}

} // namespace

int main() {
    constexpr std::uint32_t width = 64U;
    constexpr std::uint32_t height = 48U;
    const os::display::TrustedMarkRgba8 background{90U, 92U, 96U, 255U};
    std::array<os::display::TrustedMarkRgba8, width * height> pixels{};
    pixels.fill(background);

    const os::display::TrustedMarkTheme theme{
        .foundation = {12U, 14U, 20U, 255U},
        .system_chrome = {110U, 220U, 210U, 255U},
        .secure_system = {244U, 190U, 78U, 255U},
    };
    const os::display::TrustedMarkRasterTarget target{
        .pixels = pixels.data(),
        .pixel_count = pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
    };
    const os::display::PixelSize display_size{.width = width, .height = height};

    os::display::TrustedOverlaySnapshot overlay{};
    overlay.count = 2U;
    overlay.entries[0] = overlay_entry(
        2U,
        os::display::TrustedPresentation::system_chrome,
        {2, 2, 24U, 18U});
    overlay.entries[1] = overlay_entry(
        3U,
        os::display::TrustedPresentation::secure_system,
        {30, 20, 28U, 22U});

    auto system_bounds = os::display::trusted_mark_bounds(overlay.entries[0], display_size);
    assert(system_bounds);
    assert(system_bounds.value() == os::display::Rect{12, 2, 14U, 14U});
    auto secure_bounds = os::display::trusted_mark_bounds(overlay.entries[1], display_size);
    assert(secure_bounds);
    assert(secure_bounds.value() == os::display::Rect{40, 20, 18U, 18U});

    auto raster = os::display::rasterize_trusted_marks(overlay, theme, target);
    assert(raster);
    assert(raster.value().entries_seen == 2U);
    assert(raster.value().marks_drawn == 2U);
    assert(raster.value().pixel_writes > 0U);

    // Both signatures are compositor-private opaque pixels written after client
    // composition. The system mark is the compact corner cradle; secure-system
    // presentation uses a distinct accent and adds the asymmetric inner seam.
    assert(pixels[2U * width + 25U] == theme.system_chrome);
    assert(pixels[20U * width + 57U] == theme.secure_system);
    assert(pixels[27U * width + 48U] == theme.secure_system);

    // Client frame sequence alone does not change compositor-owned mark
    // placement. Normal client damage still re-runs the final mark pass; the
    // attribution-damage planner therefore remains empty for this case.
    auto same_attribution = overlay;
    same_attribution.entries[0].frame_sequence += 100U;
    same_attribution.entries[1].frame_sequence += 100U;
    auto no_attribution_damage = os::display::plan_trusted_mark_damage(
        overlay, same_attribution, display_size);
    assert(no_attribution_damage);
    assert(no_attribution_damage.value().count == 0U);

    // Moving a trusted surface damages the exact old/new mark footprints rather
    // than forcing a full-screen redraw. The compositor can restore pixels under
    // the old mark and paint the new mark after client composition.
    auto moved = overlay;
    moved.entries[0].bounds.x = 4;
    auto moved_damage = os::display::plan_trusted_mark_damage(overlay, moved, display_size);
    assert(moved_damage);
    assert(moved_damage.value().count == 2U);
    assert(moved_damage.value().rects[0] == os::display::Rect{12, 2, 14U, 14U});
    assert(moved_damage.value().rects[1] == os::display::Rect{14, 2, 14U, 14U});

    // Changing trust classification can change the compositor-owned footprint
    // even when the trusted surface bounds stay fixed, so both old and new
    // regions are invalidated.
    auto promoted = overlay;
    promoted.entries[0].presentation = os::display::TrustedPresentation::secure_system;
    auto promoted_damage = os::display::plan_trusted_mark_damage(
        overlay, promoted, display_size);
    assert(promoted_damage);
    assert(promoted_damage.value().count == 2U);
    assert(promoted_damage.value().rects[0] == os::display::Rect{12, 2, 14U, 14U});
    assert(promoted_damage.value().rects[1] == os::display::Rect{8, 2, 18U, 18U});

    // Removing a trusted surface invalidates only the old compositor mark.
    os::display::TrustedOverlaySnapshot removed{};
    removed.count = 1U;
    removed.entries[0] = overlay.entries[1];
    auto removal_damage = os::display::plan_trusted_mark_damage(
        overlay, removed, display_size);
    assert(removal_damage);
    assert(removal_damage.value().count == 1U);
    assert(removal_damage.value().rects[0] == system_bounds.value());

    // An empty overlay means ordinary application pixels receive no trust mark.
    const auto before = pixels;
    os::display::TrustedOverlaySnapshot ordinary{};
    auto no_marks = os::display::rasterize_trusted_marks(ordinary, theme, target);
    assert(no_marks);
    assert(no_marks.value().marks_drawn == 0U);
    assert(no_marks.value().pixel_writes == 0U);
    assert(pixels == before);

    auto bad_theme = theme;
    bad_theme.secure_system = bad_theme.foundation;
    auto invalid_theme = os::display::rasterize_trusted_marks(overlay, bad_theme, target);
    assert(!invalid_theme);
    expect_display_error(
        invalid_theme.error(),
        os::display::errors::invalid_trusted_mark_theme);

    auto bad_target = target;
    bad_target.pixel_count = 1U;
    auto invalid_target = os::display::rasterize_trusted_marks(overlay, theme, bad_target);
    assert(!invalid_target);
    expect_display_error(
        invalid_target.error(),
        os::display::errors::invalid_trusted_mark_target);

    auto bad_overlay = overlay;
    bad_overlay.entries[0].presentation = os::display::TrustedPresentation::none;
    auto invalid_overlay = os::display::rasterize_trusted_marks(bad_overlay, theme, target);
    assert(!invalid_overlay);
    expect_display_error(
        invalid_overlay.error(),
        os::display::errors::invalid_trusted_overlay);

    auto invalid_damage = os::display::plan_trusted_mark_damage(
        overlay, bad_overlay, display_size);
    assert(!invalid_damage);
    expect_display_error(
        invalid_damage.error(),
        os::display::errors::invalid_trusted_overlay);

    auto duplicate = overlay;
    duplicate.entries[1].surface = duplicate.entries[0].surface;
    auto duplicate_damage = os::display::plan_trusted_mark_damage(
        overlay, duplicate, display_size);
    assert(!duplicate_damage);
    expect_display_error(
        duplicate_damage.error(),
        os::display::errors::invalid_trusted_overlay);

    auto over_count = overlay;
    over_count.count = over_count.entries.size() + 1U;
    auto invalid_count = os::display::rasterize_trusted_marks(over_count, theme, target);
    assert(!invalid_count);
    expect_display_error(
        invalid_count.error(),
        os::display::errors::invalid_trusted_overlay);
    auto invalid_damage_count = os::display::plan_trusted_mark_damage(
        over_count, overlay, display_size);
    assert(!invalid_damage_count);
    expect_display_error(
        invalid_damage_count.error(),
        os::display::errors::invalid_trusted_overlay);

    // Tiny trusted surfaces still receive a bounded visible attribution pixel
    // rather than silently dropping the trust cue.
    std::array<os::display::TrustedMarkRgba8, 4U> tiny_pixels{};
    tiny_pixels.fill(background);
    os::display::TrustedOverlaySnapshot tiny_overlay{};
    tiny_overlay.count = 1U;
    tiny_overlay.entries[0] = overlay_entry(
        4U,
        os::display::TrustedPresentation::secure_system,
        {1, 1, 1U, 1U});
    const os::display::TrustedMarkRasterTarget tiny_target{
        .pixels = tiny_pixels.data(),
        .pixel_count = tiny_pixels.size(),
        .width = 2U,
        .height = 2U,
        .stride = 2U,
    };
    auto tiny = os::display::rasterize_trusted_marks(tiny_overlay, theme, tiny_target);
    assert(tiny);
    assert(tiny.value().marks_drawn == 1U);
    assert(tiny_pixels[3U] == theme.secure_system);

    auto tiny_bounds = os::display::trusted_mark_bounds(
        tiny_overlay.entries[0],
        os::display::PixelSize{.width = 2U, .height = 2U});
    assert(tiny_bounds);
    assert(tiny_bounds.value() == os::display::Rect{1, 1, 1U, 1U});

    return 0;
}
