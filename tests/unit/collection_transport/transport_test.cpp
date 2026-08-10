#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/collection/transport.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ui/types.hpp>

namespace {

os::ui::SemanticText text(const char* value) {
    auto made = os::ui::make_semantic_text(value);
    assert(made);
    return made.value();
}

void expect_protocol_error(const os::core::Error& error) {
    assert(error.domain == os::core::ErrorDomain::ipc);
    assert(error.code == os::ipc::errors::protocol_violation);
}

} // namespace

int main() {
    {
        const os::ui::CollectionDataSnapshot snapshot{
            .revision = os::ui::CollectionRevision{7U},
            .item_count = 1234U,
        };
        std::array<std::byte, os::collection::snapshot_record_size_v1> bytes{};
        auto encoded = os::collection::encode_snapshot_v1(snapshot, bytes);
        assert(encoded && encoded.value() == bytes.size());
        auto decoded = os::collection::decode_snapshot_v1(bytes);
        assert(decoded);
        assert(decoded.value().revision == snapshot.revision);
        assert(decoded.value().item_count == snapshot.item_count);

        auto malformed = bytes;
        malformed[0] = std::byte{0x02};
        auto rejected = os::collection::decode_snapshot_v1(malformed);
        assert(!rejected);
        expect_protocol_error(rejected.error());
    }

    {
        os::ui::CollectionChangeSet changes{
            .from_revision = os::ui::CollectionRevision{10U},
            .to_revision = os::ui::CollectionRevision{11U},
            .old_item_count = 3U,
            .new_item_count = 4U,
            .count = 3U,
        };
        changes.changes[0] = os::ui::CollectionChange{
            .kind = os::ui::CollectionChangeKind::insert,
            .index = 1U,
            .count = 1U,
        };
        changes.changes[1] = os::ui::CollectionChange{
            .kind = os::ui::CollectionChangeKind::move,
            .index = 3U,
            .count = 1U,
            .destination_index = 0U,
        };
        changes.changes[2] = os::ui::CollectionChange{
            .kind = os::ui::CollectionChangeKind::update,
            .index = 1U,
            .count = 2U,
        };
        assert(os::ui::collection_change_set_valid(changes));

        std::array<std::byte, os::collection::max_change_set_size_v1> bytes{};
        auto encoded = os::collection::encode_change_set_v1(changes, bytes);
        assert(encoded);
        auto decoded = os::collection::decode_change_set_v1(
            {bytes.data(), encoded.value()});
        assert(decoded);
        assert(decoded.value().from_revision == changes.from_revision);
        assert(decoded.value().to_revision == changes.to_revision);
        assert(decoded.value().old_item_count == 3U);
        assert(decoded.value().new_item_count == 4U);
        assert(decoded.value().count == 3U);
        assert(decoded.value().changes[1].kind == os::ui::CollectionChangeKind::move);
        assert(decoded.value().changes[1].destination_index == 0U);

        auto malformed = bytes;
        malformed[os::collection::change_set_header_size_v1] = std::byte{0xFF};
        auto rejected = os::collection::decode_change_set_v1(
            {malformed.data(), encoded.value()});
        assert(!rejected);
        expect_protocol_error(rejected.error());
    }

    {
        const std::uint32_t extent = os::ui::logical_from_dp(56U);
        os::ui::CollectionContentWindow window{
            .revision = os::ui::CollectionRevision{22U},
            .window = os::ui::CollectionWindow{
                .first_index = 20U,
                .count = 2U,
                .first_item_offset_q6 = -512,
                .item_extent_q6 = extent,
                .content_extent_q6 = static_cast<std::uint64_t>(extent) * 100U,
            },
            .count = 2U,
        };
        window.items[0] = os::ui::CollectionPublishedItem{
            .item_index = 20U,
            .item_key = os::ui::CollectionItemKey{1001U},
            .content = os::ui::CollectionItemContent{
                .primary_label = text("Messages"),
                .secondary_label = text("2 unread"),
                .enabled = true,
                .selected = true,
            },
        };
        window.items[1] = os::ui::CollectionPublishedItem{
            .item_index = 21U,
            .item_key = os::ui::CollectionItemKey{1002U},
            .content = os::ui::CollectionItemContent{
                .primary_label = text("Security"),
                .secondary_label = text("All protections active"),
                .enabled = true,
                .selected = false,
            },
        };

        std::array<std::byte, os::collection::max_content_window_size_v1> bytes{};
        auto encoded = os::collection::encode_content_window_v1(window, bytes);
        assert(encoded);

        os::ui::CollectionContentWindow decoded{};
        auto decoded_result = os::collection::decode_content_window_v1(
            {bytes.data(), encoded.value()}, decoded);
        assert(decoded_result);
        assert(decoded.revision == window.revision);
        assert(decoded.window.first_index == 20U);
        assert(decoded.window.count == 2U);
        assert(decoded.window.first_item_offset_q6 == -512);
        assert(decoded.count == 2U);
        assert(decoded.items[0].item_key == os::ui::CollectionItemKey{1001U});
        assert(decoded.items[0].content.primary_label.view() == "Messages");
        assert(decoded.items[0].content.secondary_label.view() == "2 unread");
        assert(decoded.items[0].content.selected);
        assert(decoded.items[1].content.primary_label.view() == "Security");
        assert(!decoded.items[1].content.selected);

        auto malformed = bytes;
        constexpr std::size_t first_item_flags_offset =
            os::collection::content_window_header_size_v1 + 4U + 8U;
        malformed[first_item_flags_offset] = std::byte{0x80};
        os::ui::CollectionContentWindow rejected_output{};
        auto rejected = os::collection::decode_content_window_v1(
            {malformed.data(), encoded.value()}, rejected_output);
        assert(!rejected);
        expect_protocol_error(rejected.error());

        auto duplicate = window;
        duplicate.items[1].item_key = duplicate.items[0].item_key;
        auto duplicate_rejected = os::collection::encode_content_window_v1(duplicate, bytes);
        assert(!duplicate_rejected);
        expect_protocol_error(duplicate_rejected.error());
    }

    {
        const os::ui::CollectionDataSnapshot oversized{
            .revision = os::ui::CollectionRevision{1U},
            .item_count = os::ui::max_collection_items + 1U,
        };
        std::array<std::byte, os::collection::snapshot_record_size_v1> bytes{};
        auto rejected = os::collection::encode_snapshot_v1(oversized, bytes);
        assert(!rejected);
        expect_protocol_error(rejected.error());
    }

    return 0;
}
