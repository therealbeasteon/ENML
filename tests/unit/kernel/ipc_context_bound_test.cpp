#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/ipc_endpoint.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "ipc context-bound: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

constexpr os::kernel::ThreadId client = 41U;
constexpr os::kernel::ThreadId server = 42U;

constexpr os::kernel::ExecutionAuthority client_authority{
    client, os::kernel::AddressSpaceIdentity{6U, 3U}};
constexpr os::kernel::ExecutionAuthority client_recycled{
    client, os::kernel::AddressSpaceIdentity{6U, 4U}};
constexpr os::kernel::ExecutionAuthority server_authority{
    server, os::kernel::AddressSpaceIdentity{7U, 5U}};

} // namespace

// M7.8.2's first increment: IpcEndpointTable::send/receive gain
// ExecutionAuthority overloads so a capability minted via
// CapabilityTable::mint(ExecutionAuthority, ...) is actually usable for IPC -
// tests/unit/kernel/ipc_endpoint_test.cpp already proves the legacy
// ThreadId-only path correctly refuses one. This file is the other half:
// proving the new path works, not just that the old one still fails closed.
//
// What this does not cover, because the code does not do it yet: reply,
// reply_transaction and take_reply still operate on bare ThreadId - an
// IpcReplySeal produced by a context-bound receive() does not yet carry the
// server's or caller's address-space generation, so nothing here can prove a
// stale generation is refused at the reply step. That is M7.8.2's remaining
// gap; docs/ROADMAP.md tracks it.
int main() {
    using namespace os::kernel;

    static_assert(client_authority.valid());
    static_assert(client_recycled.valid());
    static_assert(server_authority.valid());
    static_assert(client_authority.thread == client_recycled.thread);
    static_assert(client_authority.address_space != client_recycled.address_space);

    Rendezvous rendezvous{};
    if (!check(static_cast<bool>(rendezvous.create_thread(client)),
               "client thread creation refused")) return 1;
    if (!check(static_cast<bool>(rendezvous.create_thread(server)),
               "server thread creation refused")) return 1;

    CapabilityTable capabilities{};
    IpcEndpointTable ipc{};

    auto endpoint = ipc.create(server);
    if (!check(static_cast<bool>(endpoint), "endpoint create refused")) return 1;
    const auto object = ipc_object_id(endpoint.value());

    auto client_cap = capabilities.mint(client_authority, object, ipc_right_send, false);
    auto server_cap = capabilities.mint(server_authority, object, ipc_right_receive, false);
    if (!check(static_cast<bool>(client_cap), "client capability mint refused")) return 1;
    if (!check(static_cast<bool>(server_cap), "server capability mint refused")) return 1;

    // The legacy ThreadId-only path must still fail closed for these - the
    // same invariant ipc_endpoint_test.cpp already checks, confirmed here too
    // since this is the file a reviewer opens first for "does context-bound
    // IPC work."
    if (!check(!ipc.send(client, client_cap.value(), capabilities, rendezvous),
               "legacy send accepted a context-bound capability")) return 1;
    if (!check(!ipc.receive(server, server_cap.value(), capabilities, rendezvous),
               "legacy receive accepted a context-bound capability")) return 1;

    // A recycled generation must not be able to exercise the capability at
    // all. CapabilityTable's own tests already establish this invariant in
    // isolation; this exercises it through the IPC composition, which is the
    // new code this file exists to cover.
    if (!check(refused(
                   ipc.send(client_recycled, client_cap.value(), capabilities, rendezvous),
                   ipc_errors::invalid_capability),
               "a recycled generation sent through a stale capability")) return 1;

    // The real path: a valid, still-current ExecutionAuthority sends, is
    // received, replies, and the caller collects the reply - the whole
    // cycle, not just the capability check at the front of it.
    constexpr std::array<std::byte, 3U> request_bytes{
        std::byte{'h'}, std::byte{'i'}, std::byte{'!'}};
    auto request = IpcEnvelope::from(request_bytes);
    if (!check(static_cast<bool>(request), "request envelope refused")) return 1;

    if (!check(static_cast<bool>(ipc.send(
                   client_authority, client_cap.value(), capabilities, rendezvous, request.value())),
               "context-bound send refused")) return 1;
    if (!check(ipc.pending_call_count() == 1U, "send did not queue a pending call")) return 1;

    auto received = ipc.receive(server_authority, server_cap.value(), capabilities, rendezvous);
    if (!check(static_cast<bool>(received) && received.value().valid(),
               "context-bound receive refused")) return 1;
    if (!check(received.value().caller == client, "wrong caller delivered")) return 1;
    if (!check(received.value().request.view().size() == request_bytes.size(),
               "request payload did not survive the context-bound path")) return 1;

    constexpr std::array<std::byte, 2U> response_bytes{std::byte{'o'}, std::byte{'k'}};
    auto response = IpcEnvelope::from(response_bytes);
    if (!check(static_cast<bool>(response), "response envelope refused")) return 1;
    if (!check(static_cast<bool>(
                   ipc.reply(server, received.value().reply, rendezvous, response.value())),
               "reply refused")) return 1;

    auto collected = ipc.take_reply(client);
    if (!check(static_cast<bool>(collected), "reply not collected")) return 1;
    if (!check(collected.value().view().size() == response_bytes.size(),
               "response payload did not survive the round trip")) return 1;

    return 0;
}
