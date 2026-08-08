#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>

#include "echo_client.generated.hpp"
#include "echo_codec.generated.hpp"
#include "echo_server.generated.hpp"

namespace generated = os::test::echo;

namespace {

void ping_codec_round_trips() {
    std::array<std::byte, 64> storage{};
    os::ipc::Encoder encoder{storage};

    const generated::PingRequest request{.text = "hello"};
    const auto encode_result = generated::encode_ping_request(encoder, request);
    assert(encode_result);

    const auto bytes = encoder.written();
    assert(bytes.size() == 13U);
    assert(std::to_integer<std::uint8_t>(bytes[0]) == 9U);
    assert(std::to_integer<std::uint8_t>(bytes[4]) == 5U);

    os::ipc::Decoder decoder{bytes};
    const auto decoded = generated::decode_ping_request(decoder);
    assert(decoded);
    assert(decoded.value().text == "hello");
}

void newer_trailing_fields_are_ignored_by_old_decoder() {
    std::array<std::byte, 64> storage{};
    os::ipc::Encoder encoder{storage};

    assert(encoder.write_u32_le(13U));
    assert(encoder.write_utf8("hello", 256U));
    assert(encoder.write_u32_le(0x11223344U));

    os::ipc::Decoder decoder{encoder.written()};
    const auto decoded = generated::decode_ping_request(decoder);
    assert(decoded);
    assert(decoded.value().text == "hello");
}

void older_shorter_body_defaults_new_trailing_field_semantics() {
    std::array<std::byte, 16> storage{};
    os::ipc::Encoder encoder{storage};
    assert(encoder.write_u32_le(0U));

    os::ipc::Decoder decoder{encoder.written()};
    const auto decoded = generated::decode_identity_response(decoder);
    assert(decoded);
    assert(decoded.value().process_id == 0U);
}

void metadata_is_generated() {
    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();
    os::ipc::ClientConnection connection{channels[0]};
    generated::EchoClient client{connection};
    assert(&client.connection() == &connection);
    assert(generated::EchoClient::service_id.value() == 0x0000F001U);
    assert(generated::EchoClient::ping_operation_id == 1U);
    assert(generated::EchoClient::identify_operation_id == 2U);
}

class TestServer final : public generated::EchoServer {
public:
    os::core::Result<generated::PingResponse>
    ping(const os::ipc::RequestContext&, const generated::PingRequest& request) noexcept override {
        return generated::PingResponse{.text = request.text};
    }

    os::core::Result<generated::IdentityResponse>
    identify(const os::ipc::RequestContext& context, const generated::Empty&) noexcept override {
        return generated::IdentityResponse{
            .process_id = context.peer.process.value(),
            .principal_high = context.peer.principal.high,
            .principal_low = context.peer.principal.low,
            .user_id = context.peer.user.value(),
        };
    }

    os::core::Result<generated::IdentityResponse>
    identify_claim(const os::ipc::RequestContext& context, const generated::IdentityClaimRequest&) noexcept override {
        return identify(context, generated::Empty{});
    }
};

void server_interface_compiles() {
    TestServer server;
    const os::ipc::RequestContext context{};
    const auto response = server.ping(context, generated::PingRequest{.text = "typed"});
    assert(response);
    assert(response.value().text == "typed");
}

} // namespace

int main() {
    ping_codec_round_trips();
    newer_trailing_fields_are_ignored_by_old_decoder();
    older_shorter_body_defaults_new_trailing_field_semantics();
    metadata_is_generated();
    server_interface_compiles();
    return 0;
}
