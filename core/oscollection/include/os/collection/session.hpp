#pragma once

#include <cstddef>
#include <cstdint>

#include <os/collection/transport.hpp>
#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::collection {

struct CollectionSessionIdTag;
using CollectionSessionId = os::core::StrongId<CollectionSessionIdTag, std::uint64_t>;

// Canonical trusted consumer principal for platform-hosted semantic collection
// data. The value is an identifier, not a secret: App Manager/runtime identity
// state decides whether a process actually owns this authority.
inline constexpr os::core::PrincipalId collection_consumer_principal{
    0x454E4D4C434F4C4CULL,
    0x4543540000000001ULL,
};

// Private per-application collection capability namespace. This is not a
// globally discoverable service. F016 is intentionally distinct from the
// supervised accessibility service's F014 control namespace even though both
// protocols live only on private capabilities.
inline constexpr os::core::ServiceId application_collection_session_service_id{0x0000F016U};
inline constexpr std::uint32_t collection_session_op_snapshot = 1U;
inline constexpr std::uint32_t collection_session_op_changes = 2U;
inline constexpr std::uint32_t collection_session_op_content = 3U;

inline constexpr std::size_t session_snapshot_request_size_v1 = 12U;
inline constexpr std::size_t session_changes_request_size_v1 = 20U;
inline constexpr std::size_t session_content_request_size_v1 = 42U;

// Application-side semantic source for one private collection capability. The
// callback seams stay inside the producer process; only bounded records cross
// the channel. A complete session exposes snapshot, stable-key/content lookup,
// and one bounded revision transition.
struct CollectionSessionBackend final {
    os::ui::CollectionDataSourceBackend data {};
    os::ui::CollectionChangeSourceBackend changes {};
};

class CollectionSessionServer final {
public:
    CollectionSessionServer(
        CollectionSessionId session,
        CollectionSessionBackend backend) noexcept
        : session_(session), backend_(backend) {}

    [[nodiscard]] bool valid() const noexcept;

    // Handles exactly one synchronous request. The server never pushes
    // mutation events and owns no queue or worker; flow control is therefore
    // naturally bounded to requests the consumer explicitly issues.
    [[nodiscard]] os::core::Result<void> dispatch_once(
        os::ipc::Channel& channel,
        os::core::MutableByteSpan scratch) noexcept;

private:
    CollectionSessionId session_ {};
    CollectionSessionBackend backend_ {};
};

class CollectionSessionClient final {
public:
    CollectionSessionClient(
        os::ipc::Channel& channel,
        CollectionSessionId session) noexcept
        : connection_(channel), session_(session) {}

    [[nodiscard]] bool valid() const noexcept { return session_.value() != 0U; }

    // Consumers sample a snapshot first. If its revision changed, they may pull
    // one bounded change transition and then explicitly request only the
    // currently materialized content window. There is no producer-side backlog.
    [[nodiscard]] os::core::Result<os::ui::CollectionDataSnapshot> snapshot(
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<os::ui::CollectionChangeSet> changes_since(
        os::ui::CollectionRevision from_revision,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void> content_window(
        os::ui::CollectionRevision revision,
        const os::ui::CollectionWindow& window,
        os::ui::CollectionContentWindow& output,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
    CollectionSessionId session_ {};
};

} // namespace os::collection
