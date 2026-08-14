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
constexpr os::kernel::ExecutionAuthority server_recycled{
    server, os::kernel::AddressSpaceIdentity{7U, 6U}};

} // namespace

// M7.8.2's first increment: IpcEndpointTable::send/receive gain
// ExecutionAuthority overloads so a capability minted via
// CapabilityTable::mint(ExecutionAuthority, ...) is actually usable for IPC -
// tests/unit/kernel/ipc_endpoint_test.cpp already proves the legacy
// ThreadId-only path correctly refuses one. This file proves the new path
// works, not just that the old one still fails closed.
//
// M7.8.2's second increment: reply, reply_transaction and take_reply gain
// matching overloads. A reply seal produced by a context-bound receive()
// carries the server's generation; a pending call created by a context-bound
// send() carries the caller's, forwarded into the completed slot at reply()
// time. Both are checked below - a same-ThreadId, different-generation
// server or caller is refused exactly like a wrong thread would be, not with
// a distinguishing error, so a stale generation cannot tell "wrong thread"
// from "right thread, wrong incarnation" from the failure it gets back.
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

    // A recycled server generation must be refused at reply() with the same
    // code a wrong thread gets - checked before the real server replies, so
    // the seal is still live for the correct attempt right after.
    if (!check(refused(
                   ipc.reply(server_recycled, received.value().reply, rendezvous, response.value()),
                   ipc_errors::wrong_reply_server),
               "a recycled server generation completed a reply")) return 1;
    if (!check(ipc.active_reply_seal_count() == 1U,
               "a refused reply attempt consumed the seal anyway")) return 1;

    if (!check(static_cast<bool>(
                   ipc.reply(server_authority, received.value().reply, rendezvous, response.value())),
               "context-bound reply refused")) return 1;

    // A recycled caller generation must be refused at take_reply() the same
    // way an empty completed slot would be - not with a distinguishing
    // error - and must not consume the reply the real caller still needs.
    if (!check(refused(ipc.take_reply(client_recycled), ipc_errors::reply_unavailable),
               "a recycled caller generation collected a reply")) return 1;

    auto collected = ipc.take_reply(client_authority);
    if (!check(static_cast<bool>(collected), "context-bound take_reply refused")) return 1;
    if (!check(collected.value().view().size() == response_bytes.size(),
               "response payload did not survive the round trip")) return 1;
    if (!check(!static_cast<bool>(ipc.take_reply(client_authority)),
               "a second take_reply found something after the first collected it")) return 1;

    return 0;
}
