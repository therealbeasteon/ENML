#include <cstdlib>

#include <os/ui/system_scenes.hpp>

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

    const DeviceProfile wide_phone {
        .width_q6 = 720U * 64U,
        .height_q6 = 1280U * 64U,
        .safe_insets = InsetsQ6{},
        .posture = DevicePosture::slab,
        .width_class = WidthClass::expanded,
        .height_class = HeightClass::regular,
        .cutout = CutoutKind::none,
        .rounded_display = false,
        .one_handed_preferred = false,
    };

    const auto lock = scene_grammar(SystemSceneKind::lock_screen, tall_phone);
    const auto home = scene_grammar(SystemSceneKind::home, tall_phone);
    const auto controls = scene_grammar(SystemSceneKind::quick_controls, tall_phone);
    const auto notifications = scene_grammar(SystemSceneKind::notifications, tall_phone);
    const auto settings = scene_grammar(SystemSceneKind::settings, tall_phone);
    const auto app = scene_grammar(SystemSceneKind::app_surface, tall_phone);
    const auto switcher = scene_grammar(SystemSceneKind::app_switcher, tall_phone);

    require(system_scene_grammar_valid(lock));
    require(system_scene_grammar_valid(home));
    require(system_scene_grammar_valid(controls));
    require(system_scene_grammar_valid(notifications));
    require(system_scene_grammar_valid(settings));
    require(system_scene_grammar_valid(app));
    require(system_scene_grammar_valid(switcher));

    require(lock.wallpaper_context_visible);
    require(lock.trusted_attribution_required);
    require(lock.lower_reach_priority);

    require(home.signature.primary_contour == ContourFamily::anchor);
    require(home.signature.accent_contour == ContourFamily::sweep);
    require(home.signature.rhythm == SurfaceRhythm::quiet);

    require(controls.dominant_plane == PlaneRole::control);
    require(controls.lower_reach_priority);
    require(controls.allows_dense_content);

    require(notifications.signature.rhythm == SurfaceRhythm::flowing);
    require(settings.signature.rhythm == SurfaceRhythm::grouped);

    require(!app.trusted_attribution_required);
    require(app.dominant_plane == PlaneRole::content);

    require(switcher.entry_motion == MotionCharacter::handoff);
    require(switcher.wallpaper_context_visible);

    const auto wide_home = scene_grammar(SystemSceneKind::home, wide_phone);
    const auto wide_lock = scene_grammar(SystemSceneKind::lock_screen, wide_phone);
    require(system_scene_grammar_valid(wide_home));
    require(system_scene_grammar_valid(wide_lock));
    require(!wide_home.lower_reach_priority);
    require(!wide_lock.lower_reach_priority);

    return 0;
}
