#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

#include <os/collection/session.hpp>
#include <os/ipc/constants.hpp>
#include <os/ui/error.hpp>

namespace {

constexpr os::collection::CollectionSessionId session{0xC011EC710001ULL};
constexpr os::ui::CollectionRevision current_revision{11U};

struct Source final {
    std::array<os::ui::CollectionItemKey, 3U> keys{
        os::ui::CollectionItemKey{101U},
        os::ui::CollectionItemKey{102U},
        os::ui::CollectionItemKey{103U},
    };
};

[[nodiscard]] os::ui::SemanticText text(std::string_view value) {
    auto result = os::ui::make_semantic_text(value);
    assert(result);
    return result.value();
}

bool snapshot_source(
    void*,
    os::ui::CollectionDataSnapshot& output) noexcept {
    output = os::ui::CollectionDataSnapshot{
        .revision = current_revision,
        .item_count = 3U,
    };
    return true;
}

bool key_source(
    void* context,
    os::ui::CollectionRevision revision,
    std::uint32_t index,
    os::ui::CollectionItemKey& output) noexcept {
    const auto* source = static_cast<const Source*>(context);
    if (source == nullptr || revision != current_revision || index >= source->keys.size()) {
        return false;
    }
    output = source->keys[index];
    return true;
}

bool content_source(
    void* context,
    os::ui::CollectionRevision revision,
    std::uint32_t index,
    os::ui::CollectionItemKey key,
    os::ui::CollectionItemContent& output) noexcept {
    const auto* source = static_cast<const Source*>(context);
    if (source == nullptr || revision != current_revision || index >= source->keys.size() ||
        source->keys[index] != key) {
        return false;
    }

    const std::array<std::string_view, 3U> labels{"Alpha", "Beta", "Gamma"};
    output = os::ui::CollectionItemContent{
        .primary_label = text(labels[index]),
        .secondary_label = {},
        .enabled = true,
        .selected = index == 1U,
    };
    return true;
}

bool changes_source(
    void*,
    os::ui::CollectionRevision from,
    os::ui::CollectionChangeSet& output) noexcept {
    if (from != os::ui::CollectionRevision{10U}) return false;
    output = os::ui::CollectionChangeSet{
        .from_revision = os::ui::CollectionRevision{10U},
        .to_revision = current_revision,
        .old_item_count = 2U,
        .new_item_count = 3U,
        .count = 1U,
    };
    output.changes[0] = os::ui::CollectionChange{
        .kind = os::ui::CollectionChangeKind::insert,
        .index = 2U,
        .count = 1U,
    };
    return true;
}

} // namespace

int main() {
    Source source{};
    const os::collection::CollectionSessionBackend backend{
        .data = os::ui::CollectionDataSourceBackend{
            .context = &source,
            .snapshot = snapshot_source,
            .item_key_at = key_source,
            .item_content_at = content_source,
        },
        .changes = os::ui::CollectionChangeSourceBackend{
            .changes_since = changes_source,
        },
    };

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        os::collection::CollectionSessionServer server{session, backend};
        if (!server.valid()) ::_exit(20);
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        for (int request = 0; request < 4; ++request) {
            auto result = server.dispatch_once(pair[1], scratch);
            if (!result) ::_exit(21 + request);
        }
        ::_exit(0);
    }

    pair[1].close();
    os::collection::CollectionSessionClient client{pair[0], session};
    assert(client.valid());
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto snapshot = client.snapshot(scratch);
    assert(snapshot);
    assert(snapshot.value().revision == current_revision);
    assert(snapshot.value().item_count == 3U);

    auto changes = client.changes_since(os::ui::CollectionRevision{10U}, scratch);
    assert(changes);
    assert(changes.value().from_revision == os::ui::CollectionRevision{10U});
    assert(changes.value().to_revision == current_revision);
    assert(changes.value().count == 1U);
    assert(changes.value().changes[0].kind == os::ui::CollectionChangeKind::insert);

    const std::uint32_t extent = os::ui::logical_from_dp(56U);
    const os::ui::CollectionWindow window{
        .first_index = 1U,
        .count = 2U,
        .first_item_offset_q6 = 0,
        .item_extent_q6 = extent,
        .content_extent_q6 = static_cast<std::uint64_t>(extent) * 3U,
    };
    os::ui::CollectionContentWindow content{};
    auto content_result = client.content_window(current_revision, window, content, scratch);
    assert(content_result);
    assert(content.revision == current_revision);
    assert(content.count == 2U);
    assert(content.items[0].item_key == os::ui::CollectionItemKey{102U});
    assert(content.items[0].content.primary_label.view() == "Beta");
    assert(content.items[0].content.selected);
    assert(content.items[1].content.primary_label.view() == "Gamma");

    os::ui::CollectionContentWindow stale_output{};
    auto stale = client.content_window(
        os::ui::CollectionRevision{10U}, window, stale_output, scratch);
    assert(!stale);
    assert(stale.error().domain == os::core::ErrorDomain::ui);
    assert(stale.error().code == os::ui::errors::stale_collection_snapshot);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
