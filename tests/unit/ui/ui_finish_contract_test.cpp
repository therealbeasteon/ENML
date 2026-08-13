#include <cstdlib>

#include <os/ui/damage_policy.hpp>
#include <os/ui/frame_scheduler.hpp>
#include <os/ui/switcher_composition.hpp>

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}
} // namespace

int main() {
    using namespace os::ui;

    const DeviceProfile tall_phone {
        .width_q6 = 412U * 64U,
        .height_q6 = 915U * 64U,
        .safe_insets = InsetsQ6{.top = 48U * 64U, .right = 0U, .bottom = 24U * 64U, .left = 0U},
        .posture = DevicePosture::slab,
        .width_class = WidthClass::compact,
        .height_class = HeightClass::tall,
        .cutout = CutoutKind::centered,
        .rounded_display = true,
        .one_handed_preferred = true,
    };
    const DeviceProfile foldable {
        .width_q6 = 1536U * 64U,
        .height_q6 = 1840U * 64U,
        .safe_insets = InsetsQ6{},
        .posture = DevicePosture::unfolded,
        .width_class = WidthClass::expanded,
        .height_class = HeightClass::regular,
        .cutout = CutoutKind::hinge,
        .rounded_display = false,
        .one_handed_preferred = false,
    };
    const FrameTelemetry smooth_120hz {
        .refresh_hz = 120U,
        .input_age_ns = 1'000'000ULL,
        .cpu_render_ns = 2'000'000ULL,
        .gpu_render_ns = 2'000'000ULL,
        .present_wait_ns = 500'000ULL,
        .consecutive_misses = 0U,
        .direct_manipulation_active = false,
    };
    const RenderPressure nominal {};
    const auto smooth_frame = schedule_frame(smooth_120hz, nominal);

    const auto phone_switcher = compose_switcher_scene(tall_phone, smooth_frame);
    require(switcher_preview_composition_valid(phone_switcher, tall_phone));
    require(phone_switcher.scene.lanes == 1U);
    require(phone_switcher.visible_previews == 3U);
    require(phone_switcher.selected_task_centered);
    require(phone_switcher.preserve_task_order);

    const auto fold_switcher = compose_switcher_scene(foldable, smooth_frame);
    require(switcher_preview_composition_valid(fold_switcher, foldable));
    require(fold_switcher.scene.lanes == 2U);
    require(fold_switcher.scene.split_around_hinge);
    require(fold_switcher.visible_previews == 2U);
    require(!fold_switcher.selected_task_centered);

    auto forged = fold_switcher;
    forged.selected_task_centered = true;
    require(!switcher_preview_composition_valid(forged, foldable));

    const auto local = resolve_damage(phone_switcher.scene, 1U, false, true);
    require(damage_decision_valid(local, phone_switcher.scene));
    require(local.damage == DamageClass::local);
    require(local.preserve_unchanged_surfaces);
    require(!local.redraw_wallpaper);
    require(!local.redraw_backdrop);

    const auto scene = resolve_damage(phone_switcher.scene, phone_switcher.scene.count, true, false);
    require(damage_decision_valid(scene, phone_switcher.scene));
    require(scene.damage == DamageClass::scene);
    require(scene.redraw_wallpaper);
    require(!scene.preserve_unchanged_surfaces);

    auto invalid_damage = local;
    invalid_damage.redraw_backdrop = true;
    require(!damage_decision_valid(invalid_damage, phone_switcher.scene));

    return 0;
}
