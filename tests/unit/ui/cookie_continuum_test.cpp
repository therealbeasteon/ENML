#include <cstdlib>

#include <os/ui/cookie_continuum.hpp>
#include <os/ui/home.hpp>
#include <os/ui/layout_policy.hpp>

namespace { void require(bool condition) { if (!condition) std::abort(); } }

int main() {
    using namespace os::ui;
    const CookieContinuumPolicy native {};
    require(cookie_continuum_policy_valid(native));

    CookieContinuumPolicy android_like = native; android_like.fixed_page_grid = true;
    require(!cookie_continuum_policy_valid(android_like));
    CookieContinuumPolicy conventional_dock = native; conventional_dock.permanent_icon_dock = true;
    require(!cookie_continuum_policy_valid(conventional_dock));
    CookieContinuumPolicy one_ui_like = native; one_ui_like.card_wall_primary = true; one_ui_like.bottom_sheet_primary_navigation = true;
    require(!cookie_continuum_policy_valid(one_ui_like));
    CookieContinuumPolicy glass_identity = native; glass_identity.glass_required = true;
    require(!cookie_continuum_policy_valid(glass_identity));
    CookieContinuumPolicy uniform_mask = native; uniform_mask.uniform_icon_mask_required = true;
    require(!cookie_continuum_policy_valid(uniform_mask));
    CookieContinuumPolicy unstable_prediction = native; unstable_prediction.stable_spatial_order_required = false;
    require(!cookie_continuum_policy_valid(unstable_prediction));

    ContinuumNode stable_node {.object_id=10U,.band=ContinuumBand::pinned,.anchor_x_q10=300U,.anchor_y_q10=640U,.visual_weight_q10=600U,.user_stable=true};
    require(continuum_node_valid(stable_node));
    ContinuumNode predicted_pin = stable_node; predicted_pin.object_id=11U; predicted_pin.user_stable=false;
    require(!continuum_node_valid(predicted_pin));
    ContinuumNode now_node = predicted_pin; now_node.band=ContinuumBand::now;
    require(continuum_node_valid(now_node));
    ContinuumNode outside_field = now_node; outside_field.anchor_x_q10=1025U;
    require(!continuum_node_valid(outside_field));

    ContinuumThread contextual_thread {.from_object_id=10U,.to_object_id=11U,.relation=ThreadRelation::contextual,.permanently_visible=false};
    require(continuum_thread_valid(contextual_thread));
    ContinuumThread decorative_wire=contextual_thread; decorative_wire.permanently_visible=true;
    require(!continuum_thread_valid(decorative_wire));
    ContinuumThread trusted_thread {.from_object_id=10U,.to_object_id=12U,.relation=ThreadRelation::trusted,.permanently_visible=true};
    require(continuum_thread_valid(trusted_thread));

    const DeviceProfile phone {.width_q6=412U*64U,.height_q6=915U*64U,.safe_insets=InsetsQ6{.top=48U*64U,.right=0U,.bottom=24U*64U,.left=0U},.posture=DevicePosture::slab,.width_class=WidthClass::compact,.height_class=HeightClass::tall,.cutout=CutoutKind::centered,.rounded_display=true,.one_handed_preferred=true};
    const auto right_index=resolve_cookie_index(phone,false);
    require(cookie_index_policy_valid(right_index)); require(right_index.side==ThreadSide::right); require(right_index.default_view==IndexView::intent); require(right_index.alphabetic_fallback); require(!right_index.prediction_may_reorder_main_index);
    const auto left_index=resolve_cookie_index(phone,true); require(left_index.side==ThreadSide::left);
    const auto phone_layout=resolve_home_layout_policy(phone);
    require(phone_layout.composition==HomeComposition::reach_biased); require(phone_layout.index_edge_reachable);
    require(preferred_reach_zone(phone,HomeRegion::index)==ReachZone::primary); require(preferred_reach_zone(phone,HomeRegion::now)==ReachZone::primary); require(preferred_reach_zone(phone,HomeRegion::pinned)==ReachZone::secondary);

    HomeObject pinned {.id=HomeObjectId{1U},.kind=HomeObjectKind::application,.preferred_region=HomeRegion::pinned,.privacy=HomePrivacyClass::public_metadata,.preferred_span_x=1U,.preferred_span_y=1U,.user_pinned=true,.remote_enrichment_allowed=false};
    require(home_object_valid(pinned));
    HomeObject predicted_pinned=pinned; predicted_pinned.id=HomeObjectId{2U}; predicted_pinned.user_pinned=false; require(!home_object_valid(predicted_pinned));
    HomeObject contextual=predicted_pinned; contextual.preferred_region=HomeRegion::now; require(home_object_valid(contextual));
    HomeObject secret_remote=contextual; secret_remote.id=HomeObjectId{3U}; secret_remote.privacy=HomePrivacyClass::secret; secret_remote.remote_enrichment_allowed=true; require(!home_object_valid(secret_remote));

    const DeviceProfile foldable {.width_q6=1536U*64U,.height_q6=1840U*64U,.safe_insets=InsetsQ6{},.posture=DevicePosture::unfolded,.width_class=WidthClass::expanded,.height_class=HeightClass::regular,.cutout=CutoutKind::hinge,.rounded_display=false,.one_handed_preferred=false};
    const auto fold_index=resolve_cookie_index(foldable,false); require(cookie_index_policy_valid(fold_index)); require(fold_index.side==ThreadSide::split);
    const auto fold_layout=resolve_home_layout_policy(foldable); require(fold_layout.composition==HomeComposition::seam_split); require(fold_layout.reserve_center_seam);
    CookieIndexPolicy reshuffling=right_index; reshuffling.prediction_may_reorder_main_index=true; require(!cookie_index_policy_valid(reshuffling));
    return 0;
}
