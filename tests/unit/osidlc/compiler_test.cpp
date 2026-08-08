#include <cassert>
#include <string_view>

#include <osidlc/compiler.hpp>

namespace {

constexpr std::string_view valid_schema = R"OSIDL(
package os.test.echo;

struct Empty {
}

struct PingRequest {
    string<256> text = 1;
}

struct PingResponse {
    string<256> text = 1;
}

service Echo {
    id 0x0000F001;
    version 1;

    operation Ping = 1 {
        request PingRequest;
        response PingResponse;
    }
}
)OSIDL";

void valid_schema_generates_deterministic_artifacts() {
    const auto first = osidlc::compile_text(valid_schema, "valid.osidl");
    const auto second = osidlc::compile_text(valid_schema, "valid.osidl");

    assert(first.success);
    assert(second.success);
    assert(first.files.types_header == second.files.types_header);
    assert(first.files.codec_source == second.files.codec_source);
    assert(first.files.abi_json == second.files.abi_json);
    assert(first.files.types_header.find("struct PingRequest") != std::string::npos);
    assert(first.files.codec_source.find("encode_ping_request") != std::string::npos);
    assert(first.files.client_header.find("service_id{0xf001U}") != std::string::npos);
    assert(first.files.abi_json.find("\"wire_schema\": \"appendable-positional-v0\"") != std::string::npos);
    assert(first.files.abi_json.find("\"id\": 61441") != std::string::npos);
}

void duplicate_field_ids_are_rejected() {
    constexpr std::string_view source = R"OSIDL(
package os.test.bad;
struct A {
    u32 first = 1;
    u64 second = 1;
}
service Bad {
    id 1;
    version 1;
    operation Test = 1 { request A; response A; }
}
)OSIDL";
    const auto result = osidlc::compile_text(source, "duplicate.osidl");
    assert(!result.success);
    assert(result.diagnostic.find("duplicate field id") != std::string::npos);
}

void non_append_field_order_is_rejected() {
    constexpr std::string_view source = R"OSIDL(
package os.test.bad;
struct A {
    u32 second = 2;
    u32 first = 1;
}
service Bad {
    id 1;
    version 1;
    operation Test = 1 { request A; response A; }
}
)OSIDL";
    const auto result = osidlc::compile_text(source, "order.osidl");
    assert(!result.success);
    assert(result.diagnostic.find("strictly increasing ids") != std::string::npos);
}

void unknown_operation_types_are_rejected() {
    constexpr std::string_view source = R"OSIDL(
package os.test.bad;
struct Empty {}
service Bad {
    id 1;
    version 1;
    operation Test = 1 { request Missing; response Empty; }
}
)OSIDL";
    const auto result = osidlc::compile_text(source, "unknown.osidl");
    assert(!result.success);
    assert(result.diagnostic.find("unknown request type") != std::string::npos);
}

void oversized_struct_is_rejected() {
    constexpr std::string_view source = R"OSIDL(
package os.test.bad;
struct Huge {
    string<65536> text = 1;
}
service Bad {
    id 1;
    version 1;
    operation Test = 1 { request Huge; response Huge; }
}
)OSIDL";
    const auto result = osidlc::compile_text(source, "huge.osidl");
    assert(!result.success);
    assert(result.diagnostic.find("exceeds 64 KiB") != std::string::npos);
}

void malformed_input_has_location() {
    constexpr std::string_view source = "package os.test;\nstruct A {\n u32 x = ;\n}\n";
    const auto result = osidlc::compile_text(source, "broken.osidl");
    assert(!result.success);
    assert(result.diagnostic.find("broken.osidl:3:") != std::string::npos);
}

} // namespace

int main() {
    valid_schema_generates_deterministic_artifacts();
    duplicate_field_ids_are_rejected();
    non_append_field_order_is_rejected();
    unknown_operation_types_are_rejected();
    oversized_struct_is_rejected();
    malformed_input_has_location();
    return 0;
}
