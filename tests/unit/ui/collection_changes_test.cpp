#include <os/ui/collection_changes.hpp>

#include <cassert>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

struct ChangeSource final {
    os::ui::CollectionChangeSet changes {};
    bool fail {false};
};

bool changes_since(
    void* context,
    os::ui::CollectionRevision from_revision,
    os::ui::CollectionChangeSet& output) noexcept {
    auto* source = static_cast<ChangeSource*>(context);
    if (source == nullptr || source->fail || source->changes.from_revision != from_revision) {
        return false;
    }
    output = source->changes;
    return true;
}

os::ui::CollectionWindow window(std::uint32_t first, std::uint16_t count) {
    return os::ui::CollectionWindow{
        .first_index = first,
        .count = count,
        .first_item_offset_q6 = 0,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .content_extent_q6 = 0U,
    };
}

} // namespace

int main() {
    os::ui::CollectionChangeSet transition{};
    transition.from_revision = os::ui::CollectionRevision{10U};
    transition.to_revision = os::ui::CollectionRevision{11U};
    transition.old_item_count = 5U;
    transition.new_item_count = 6U;
    transition.count = 3U;
    transition.changes[0] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::update,
        .index = 1U,
        .count = 1U,
    };
    transition.changes[1] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::insert,
        .index = 5U,
        .count = 2U,
    };
    transition.changes[2] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::remove,
        .index = 3U,
        .count = 1U,
    };
    assert(os::ui::collection_change_set_valid(transition));

    auto visible_affected = os::ui::collection_change_affects_window(
        transition, window(1U, 2U));
    assert(visible_affected && visible_affected.value());

    ChangeSource source{.changes = transition};
    auto fetched = os::ui::collection_changes_since(
        os::ui::CollectionRevision{10U},
        os::ui::CollectionChangeSourceBackend{
            .context = &source,
            .changes_since = changes_since,
        });
    assert(fetched);
    assert(fetched.value().to_revision == os::ui::CollectionRevision{11U});

    source.fail = true;
    auto failed = os::ui::collection_changes_since(
        os::ui::CollectionRevision{10U},
        os::ui::CollectionChangeSourceBackend{
            .context = &source,
            .changes_since = changes_since,
        });
    assert(!failed);
    expect_ui_error(failed.error(), os::ui::errors::collection_change_source_failed);
    source.fail = false;

    auto missing = os::ui::collection_changes_since(
        os::ui::CollectionRevision{10U}, {});
    assert(!missing);
    expect_ui_error(missing.error(), os::ui::errors::collection_change_source_failed);

    os::ui::CollectionChangeSet distant{};
    distant.from_revision = os::ui::CollectionRevision{20U};
    distant.to_revision = os::ui::CollectionRevision{21U};
    distant.old_item_count = 10U;
    distant.new_item_count = 10U;
    distant.count = 1U;
    distant.changes[0] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::update,
        .index = 8U,
        .count = 1U,
    };
    assert(os::ui::collection_change_set_valid(distant));
    auto distant_result = os::ui::collection_change_affects_window(
        distant, window(2U, 3U));
    assert(distant_result && !distant_result.value());

    os::ui::CollectionChangeSet append{};
    append.from_revision = os::ui::CollectionRevision{30U};
    append.to_revision = os::ui::CollectionRevision{31U};
    append.old_item_count = 10U;
    append.new_item_count = 11U;
    append.count = 1U;
    append.changes[0] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::insert,
        .index = 10U,
        .count = 1U,
    };
    assert(os::ui::collection_change_set_valid(append));
    auto append_result = os::ui::collection_change_affects_window(
        append, window(2U, 3U));
    assert(append_result && !append_result.value());

    auto insert_before = append;
    insert_before.to_revision = os::ui::CollectionRevision{32U};
    insert_before.changes[0].index = 1U;
    auto insert_before_result = os::ui::collection_change_affects_window(
        insert_before, window(2U, 3U));
    assert(insert_before_result && insert_before_result.value());

    os::ui::CollectionChangeSet moved{};
    moved.from_revision = os::ui::CollectionRevision{40U};
    moved.to_revision = os::ui::CollectionRevision{41U};
    moved.old_item_count = 10U;
    moved.new_item_count = 10U;
    moved.count = 1U;
    moved.changes[0] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::move,
        .index = 8U,
        .count = 1U,
        .destination_index = 7U,
    };
    assert(os::ui::collection_change_set_valid(moved));
    auto moved_result = os::ui::collection_change_affects_window(
        moved, window(2U, 3U));
    assert(moved_result && !moved_result.value());

    os::ui::CollectionChangeSet reset{};
    reset.from_revision = os::ui::CollectionRevision{50U};
    reset.to_revision = os::ui::CollectionRevision{51U};
    reset.old_item_count = 10U;
    reset.new_item_count = 4U;
    reset.count = 1U;
    reset.changes[0] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::reset,
    };
    assert(os::ui::collection_change_set_valid(reset));
    auto reset_result = os::ui::collection_change_affects_window(
        reset, window(2U, 3U));
    assert(reset_result && reset_result.value());

    auto bad_count = transition;
    bad_count.new_item_count = 7U;
    assert(!os::ui::collection_change_set_valid(bad_count));
    auto invalid_window_check = os::ui::collection_change_affects_window(
        bad_count, window(1U, 2U));
    assert(!invalid_window_check);
    expect_ui_error(
        invalid_window_check.error(),
        os::ui::errors::invalid_collection_change);

    auto bad_reset = reset;
    bad_reset.count = 2U;
    bad_reset.changes[1] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::update,
        .index = 0U,
        .count = 1U,
    };
    assert(!os::ui::collection_change_set_valid(bad_reset));

    return 0;
}
