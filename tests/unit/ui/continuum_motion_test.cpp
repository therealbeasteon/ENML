#include <cstdlib>

#include <os/ui/continuum_motion.hpp>

namespace { void require(bool condition) { if (!condition) std::abort(); } }

int main() {
    using namespace os::ui;

    ContinuumTransition opening {
        .kind = ContinuumTransitionKind::node_to_app,
        .source_object_id = 42U,
        .progress_q10 = 430U,
        .velocity_q10 = 240,
        .preserve_source_geometry = true,
        .interruptible = true,
    };
    require(continuum_transition_valid(opening));

    const auto reversed = retarget_continuum_transition(opening, ContinuumTransitionKind::app_to_node);
    require(continuum_transition_valid(reversed));
    require(reversed.progress_q10 == opening.progress_q10);
    require(reversed.velocity_q10 == -opening.velocity_q10);

    ContinuumTransition index {
        .kind = ContinuumTransitionKind::index_reveal,
        .source_object_id = 0U,
        .progress_q10 = 600U,
        .velocity_q10 = 180,
        .preserve_source_geometry = true,
        .interruptible = true,
    };
    require(continuum_transition_valid(index));
    const auto retract = retarget_continuum_transition(index, ContinuumTransitionKind::index_retract);
    require(retract.progress_q10 == 600U);
    require(retract.velocity_q10 == -180);

    auto restart_like = opening;
    restart_like.interruptible = false;
    require(!continuum_transition_valid(restart_like));

    auto disconnected = opening;
    disconnected.preserve_source_geometry = false;
    require(!continuum_transition_valid(disconnected));

    auto invalid_progress = opening;
    invalid_progress.progress_q10 = 1025U;
    require(!continuum_transition_valid(invalid_progress));

    opening.progress_q10 = 1024U;
    require(continuum_transition_complete(opening));
    return 0;
}
