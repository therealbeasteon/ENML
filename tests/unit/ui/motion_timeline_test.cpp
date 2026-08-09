#include <os/ui/motion.hpp>

#include <cassert>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

} // namespace

int main() {
    constexpr std::uint64_t start = 1'000'000'000ULL;

    auto none = os::ui::begin_motion(os::ui::MotionRole::none, false, start);
    assert(none);
    assert(!none.value().active);
    assert(none.value().start_ns == none.value().end_ns);
    auto none_sample = os::ui::sample_motion(none.value(), start, start + 16'000'000ULL);
    assert(none_sample);
    assert(none_sample.value().complete);
    assert(!none_sample.value().request_next_frame);
    assert(none_sample.value().progress_q16 == os::ui::motion_progress_one_q16);

    auto responsive = os::ui::begin_motion(os::ui::MotionRole::responsive, false, start);
    assert(responsive);
    assert(responsive.value().active);
    assert(responsive.value().metrics.duration_ms == 180U);
    assert(responsive.value().end_ns == start + 180'000'000ULL);

    auto early = os::ui::sample_motion(
        responsive.value(), start + 45'000'000ULL, start + 50'000'000ULL);
    assert(early);
    assert(!early.value().complete);
    assert(early.value().progress_q16 > 0U);
    assert(early.value().progress_q16 < os::ui::motion_progress_one_q16);
    assert(early.value().request_next_frame);
    assert(early.value().next_frame_ns == start + 50'000'000ULL);

    auto stale_tick = os::ui::sample_motion(
        responsive.value(), start + 60'000'000ULL, start + 60'000'000ULL);
    assert(stale_tick);
    assert(!stale_tick.value().complete);
    assert(!stale_tick.value().request_next_frame);
    assert(stale_tick.value().next_frame_ns == 0U);

    auto final_tick = os::ui::sample_motion(
        responsive.value(), start + 170'000'000ULL, start + 250'000'000ULL);
    assert(final_tick);
    assert(final_tick.value().request_next_frame);
    assert(final_tick.value().next_frame_ns == responsive.value().end_ns);

    auto finished = os::ui::sample_motion(
        responsive.value(), responsive.value().end_ns, responsive.value().end_ns + 16'000'000ULL);
    assert(finished);
    assert(finished.value().complete);
    assert(finished.value().progress_q16 == os::ui::motion_progress_one_q16);
    assert(!finished.value().request_next_frame);

    auto reduced = os::ui::begin_motion(os::ui::MotionRole::transition, true, start);
    assert(reduced);
    assert(reduced.value().active);
    assert(reduced.value().metrics.duration_ms == 80U);
    assert(!reduced.value().metrics.spatial_motion_allowed);

    auto before_start = os::ui::sample_motion(
        responsive.value(), start - 1U, start + 1U);
    assert(!before_start);
    expect_ui_error(before_start.error(), os::ui::errors::invalid_motion_timeline);

    auto malformed = responsive.value();
    malformed.end_ns = malformed.start_ns;
    auto malformed_sample = os::ui::sample_motion(
        malformed, start, start + 16'000'000ULL);
    assert(!malformed_sample);
    expect_ui_error(malformed_sample.error(), os::ui::errors::invalid_motion_timeline);

    auto overflow = os::ui::begin_motion(
        os::ui::MotionRole::reveal,
        false,
        UINT64_MAX - 100U);
    assert(!overflow);
    expect_ui_error(overflow.error(), os::ui::errors::invalid_motion_timeline);

    return 0;
}
