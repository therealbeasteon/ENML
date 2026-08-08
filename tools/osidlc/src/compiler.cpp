#include <osidlc/compiler.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace osidlc {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kMaxInlinePayload = 64ULL * 1024ULL;

enum class TokenKind {
    identifier,
    number,
    symbol,
    end,
};

struct Token final {
    TokenKind kind {TokenKind::end};
    std::string text {};
    std::size_t line {1};
    std::size_t column {1};
};

class Lexer final {
public:
    Lexer(std::string_view source, std::string_view source_name)
        : source_(source), source_name_(source_name) {}

    [[nodiscard]] std::vector<Token> lex(std::string& diagnostic) {
        std::vector<Token> tokens;
        while (true) {
            skip_space_and_comments();
            if (offset_ >= source_.size()) {
                tokens.push_back(Token{TokenKind::end, "", line_, column_});
                return tokens;
            }

            const auto start_line = line_;
            const auto start_column = column_;
            const char ch = source_[offset_];

            if (is_identifier_start(ch)) {
                const auto start = offset_;
                advance();
                while (offset_ < source_.size() && is_identifier_continue(source_[offset_])) {
                    advance();
                }
                tokens.push_back(Token{
                    TokenKind::identifier,
                    std::string{source_.substr(start, offset_ - start)},
                    start_line,
                    start_column,
                });
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                const auto start = offset_;
                advance();
                if (ch == '0' && offset_ < source_.size() &&
                    (source_[offset_] == 'x' || source_[offset_] == 'X')) {
                    advance();
                    const auto hex_start = offset_;
                    while (offset_ < source_.size() &&
                           std::isxdigit(static_cast<unsigned char>(source_[offset_])) != 0) {
                        advance();
                    }
                    if (hex_start == offset_) {
                        diagnostic = format_error(start_line, start_column, "expected hexadecimal digits after 0x");
                        return {};
                    }
                } else {
                    while (offset_ < source_.size() &&
                           std::isdigit(static_cast<unsigned char>(source_[offset_])) != 0) {
                        advance();
                    }
                }
                tokens.push_back(Token{
                    TokenKind::number,
                    std::string{source_.substr(start, offset_ - start)},
                    start_line,
                    start_column,
                });
                continue;
            }

            if (is_symbol(ch)) {
                tokens.push_back(Token{TokenKind::symbol, std::string(1, ch), start_line, start_column});
                advance();
                continue;
            }

            diagnostic = format_error(start_line, start_column, "unexpected character");
            return {};
        }
    }

private:
    std::string_view source_;
    std::string source_name_;
    std::size_t offset_ {0};
    std::size_t line_ {1};
    std::size_t column_ {1};

    static bool is_identifier_start(char ch) noexcept {
        return ch == '_' || std::isalpha(static_cast<unsigned char>(ch)) != 0;
    }

    static bool is_identifier_continue(char ch) noexcept {
        return ch == '_' || std::isalnum(static_cast<unsigned char>(ch)) != 0;
    }

    static bool is_symbol(char ch) noexcept {
        switch (ch) {
        case '.':
        case ';':
        case '{':
        case '}':
        case '<':
        case '>':
        case '=':
            return true;
        default:
            return false;
        }
    }

    void advance() noexcept {
        if (offset_ >= source_.size()) {
            return;
        }
        if (source_[offset_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++offset_;
    }

    void skip_space_and_comments() noexcept {
        while (offset_ < source_.size()) {
            if (std::isspace(static_cast<unsigned char>(source_[offset_])) != 0) {
                advance();
                continue;
            }
            if (source_[offset_] == '/' && offset_ + 1U < source_.size() &&
                source_[offset_ + 1U] == '/') {
                while (offset_ < source_.size() && source_[offset_] != '\n') {
                    advance();
                }
                continue;
            }
            break;
        }
    }

    [[nodiscard]] std::string format_error(
        std::size_t line,
        std::size_t column,
        std::string_view message) const {
        std::ostringstream out;
        out << source_name_ << ':' << line << ':' << column << ": " << message;
        return out.str();
    }
};

enum class FieldTypeKind {
    u32,
    u64,
    string,
};

struct FieldType final {
    FieldTypeKind kind {FieldTypeKind::u32};
    std::uint32_t string_maximum {0};
};

struct FieldDecl final {
    std::string name {};
    FieldType type {};
    std::uint32_t id {0};
    std::size_t line {0};
    std::size_t column {0};
};

struct StructDecl final {
    std::string name {};
    std::vector<FieldDecl> fields {};
    std::size_t line {0};
    std::size_t column {0};
};

struct OperationDecl final {
    std::string name {};
    std::uint32_t id {0};
    std::string request_type {};
    std::string response_type {};
    std::size_t line {0};
    std::size_t column {0};
};

struct ServiceDecl final {
    std::string name {};
    std::uint32_t id {0};
    std::uint32_t version {0};
    std::vector<OperationDecl> operations {};
    std::size_t line {0};
    std::size_t column {0};
};

struct Schema final {
    std::string package {};
    std::vector<StructDecl> structs {};
    std::optional<ServiceDecl> service {};
};

class Parser final {
public:
    Parser(std::vector<Token> tokens, std::string_view source_name)
        : tokens_(std::move(tokens)), source_name_(source_name) {}

    [[nodiscard]] bool parse(Schema& schema, std::string& diagnostic) {
        bool have_package = false;
        while (!at_end()) {
            if (matches_identifier("package")) {
                if (have_package) {
                    return fail(current(), "duplicate package declaration", diagnostic);
                }
                if (!parse_package(schema.package, diagnostic)) {
                    return false;
                }
                have_package = true;
                continue;
            }
            if (matches_identifier("struct")) {
                StructDecl structure;
                if (!parse_struct(structure, diagnostic)) {
                    return false;
                }
                schema.structs.push_back(std::move(structure));
                continue;
            }
            if (matches_identifier("service")) {
                if (schema.service.has_value()) {
                    return fail(current(), "M0 OSIDL supports exactly one service per file", diagnostic);
                }
                ServiceDecl service;
                if (!parse_service(service, diagnostic)) {
                    return false;
                }
                schema.service = std::move(service);
                continue;
            }
            return fail(current(), "expected package, struct, or service declaration", diagnostic);
        }

        if (!have_package) {
            diagnostic = std::string{source_name_} + ":1:1: missing package declaration";
            return false;
        }
        if (!schema.service.has_value()) {
            diagnostic = std::string{source_name_} + ":1:1: missing service declaration";
            return false;
        }
        return true;
    }

private:
    std::vector<Token> tokens_;
    std::string source_name_;
    std::size_t index_ {0};

    [[nodiscard]] const Token& current() const noexcept { return tokens_[index_]; }
    [[nodiscard]] bool at_end() const noexcept { return current().kind == TokenKind::end; }

    [[nodiscard]] bool matches_identifier(std::string_view text) const noexcept {
        return current().kind == TokenKind::identifier && current().text == text;
    }

    void advance() noexcept {
        if (!at_end()) {
            ++index_;
        }
    }

    [[nodiscard]] bool consume_identifier(std::string& value, std::string& diagnostic) {
        if (current().kind != TokenKind::identifier) {
            return fail(current(), "expected identifier", diagnostic);
        }
        value = current().text;
        advance();
        return true;
    }

    [[nodiscard]] bool consume_keyword(std::string_view keyword, std::string& diagnostic) {
        if (!matches_identifier(keyword)) {
            std::string message = "expected '";
            message += keyword;
            message += "'";
            return fail(current(), message, diagnostic);
        }
        advance();
        return true;
    }

    [[nodiscard]] bool consume_symbol(char symbol, std::string& diagnostic) {
        if (current().kind != TokenKind::symbol || current().text.size() != 1U ||
            current().text[0] != symbol) {
            std::string message = "expected '";
            message.push_back(symbol);
            message += "'";
            return fail(current(), message, diagnostic);
        }
        advance();
        return true;
    }

    [[nodiscard]] bool consume_number(std::uint64_t& value, std::string& diagnostic) {
        if (current().kind != TokenKind::number) {
            return fail(current(), "expected integer", diagnostic);
        }
        const auto token = current();
        int base = 10;
        std::string_view text{token.text};
        if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            text.remove_prefix(2);
        }
        std::uint64_t parsed = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed, base);
        if (ec != std::errc{} || ptr != text.data() + text.size()) {
            return fail(token, "invalid integer", diagnostic);
        }
        value = parsed;
        advance();
        return true;
    }

    [[nodiscard]] bool parse_package(std::string& package, std::string& diagnostic) {
        if (!consume_keyword("package", diagnostic)) {
            return false;
        }
        std::string segment;
        if (!consume_identifier(segment, diagnostic)) {
            return false;
        }
        package = segment;
        while (current().kind == TokenKind::symbol && current().text == ".") {
            advance();
            if (!consume_identifier(segment, diagnostic)) {
                return false;
            }
            package += '.';
            package += segment;
        }
        return consume_symbol(';', diagnostic);
    }

    [[nodiscard]] bool parse_type(FieldType& type, std::string& diagnostic) {
        if (matches_identifier("u32")) {
            type.kind = FieldTypeKind::u32;
            advance();
            return true;
        }
        if (matches_identifier("u64")) {
            type.kind = FieldTypeKind::u64;
            advance();
            return true;
        }
        if (matches_identifier("string")) {
            type.kind = FieldTypeKind::string;
            advance();
            if (!consume_symbol('<', diagnostic)) {
                return false;
            }
            std::uint64_t maximum = 0;
            if (!consume_number(maximum, diagnostic)) {
                return false;
            }
            if (maximum == 0 || maximum > kMaxInlinePayload ||
                maximum > std::numeric_limits<std::uint32_t>::max()) {
                return fail(current(), "string bound must be between 1 and 65536 bytes", diagnostic);
            }
            type.string_maximum = static_cast<std::uint32_t>(maximum);
            return consume_symbol('>', diagnostic);
        }
        return fail(current(), "expected field type u32, u64, or string<N>", diagnostic);
    }

    [[nodiscard]] bool parse_struct(StructDecl& structure, std::string& diagnostic) {
        const auto location = current();
        if (!consume_keyword("struct", diagnostic)) {
            return false;
        }
        structure.line = location.line;
        structure.column = location.column;
        if (!consume_identifier(structure.name, diagnostic) || !consume_symbol('{', diagnostic)) {
            return false;
        }

        while (!(current().kind == TokenKind::symbol && current().text == "}")) {
            if (at_end()) {
                return fail(current(), "unterminated struct", diagnostic);
            }
            FieldDecl field;
            field.line = current().line;
            field.column = current().column;
            if (!parse_type(field.type, diagnostic)) {
                return false;
            }
            if (!consume_identifier(field.name, diagnostic) || !consume_symbol('=', diagnostic)) {
                return false;
            }
            std::uint64_t field_id = 0;
            if (!consume_number(field_id, diagnostic)) {
                return false;
            }
            if (field_id == 0 || field_id > std::numeric_limits<std::uint32_t>::max()) {
                return fail(current(), "field id must be in range 1..2^32-1", diagnostic);
            }
            field.id = static_cast<std::uint32_t>(field_id);
            if (!consume_symbol(';', diagnostic)) {
                return false;
            }
            structure.fields.push_back(std::move(field));
        }
        advance();
        return true;
    }

    [[nodiscard]] bool parse_operation(OperationDecl& operation, std::string& diagnostic) {
        const auto location = current();
        if (!consume_keyword("operation", diagnostic)) {
            return false;
        }
        operation.line = location.line;
        operation.column = location.column;
        if (!consume_identifier(operation.name, diagnostic) || !consume_symbol('=', diagnostic)) {
            return false;
        }
        std::uint64_t operation_id = 0;
        if (!consume_number(operation_id, diagnostic)) {
            return false;
        }
        if (operation_id == 0 || operation_id > std::numeric_limits<std::uint32_t>::max()) {
            return fail(current(), "operation id must be in range 1..2^32-1", diagnostic);
        }
        operation.id = static_cast<std::uint32_t>(operation_id);
        if (!consume_symbol('{', diagnostic)) {
            return false;
        }
        if (!consume_keyword("request", diagnostic) ||
            !consume_identifier(operation.request_type, diagnostic) ||
            !consume_symbol(';', diagnostic) ||
            !consume_keyword("response", diagnostic) ||
            !consume_identifier(operation.response_type, diagnostic) ||
            !consume_symbol(';', diagnostic) ||
            !consume_symbol('}', diagnostic)) {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool parse_service(ServiceDecl& service, std::string& diagnostic) {
        const auto location = current();
        if (!consume_keyword("service", diagnostic)) {
            return false;
        }
        service.line = location.line;
        service.column = location.column;
        if (!consume_identifier(service.name, diagnostic) || !consume_symbol('{', diagnostic)) {
            return false;
        }

        bool have_id = false;
        bool have_version = false;
        while (!(current().kind == TokenKind::symbol && current().text == "}")) {
            if (at_end()) {
                return fail(current(), "unterminated service", diagnostic);
            }
            if (matches_identifier("id")) {
                if (have_id) {
                    return fail(current(), "duplicate service id", diagnostic);
                }
                advance();
                std::uint64_t id = 0;
                if (!consume_number(id, diagnostic)) {
                    return false;
                }
                if (id == 0 || id > std::numeric_limits<std::uint32_t>::max()) {
                    return fail(current(), "service id must be in range 1..2^32-1", diagnostic);
                }
                service.id = static_cast<std::uint32_t>(id);
                have_id = true;
                if (!consume_symbol(';', diagnostic)) {
                    return false;
                }
                continue;
            }
            if (matches_identifier("version")) {
                if (have_version) {
                    return fail(current(), "duplicate service version", diagnostic);
                }
                advance();
                std::uint64_t version = 0;
                if (!consume_number(version, diagnostic)) {
                    return false;
                }
                if (version == 0 || version > std::numeric_limits<std::uint16_t>::max()) {
                    return fail(current(), "service version must be in range 1..65535", diagnostic);
                }
                service.version = static_cast<std::uint32_t>(version);
                have_version = true;
                if (!consume_symbol(';', diagnostic)) {
                    return false;
                }
                continue;
            }
            if (matches_identifier("operation")) {
                OperationDecl operation;
                if (!parse_operation(operation, diagnostic)) {
                    return false;
                }
                service.operations.push_back(std::move(operation));
                continue;
            }
            return fail(current(), "expected service id, version, or operation", diagnostic);
        }
        advance();
        if (!have_id || !have_version) {
            return fail(location, "service requires id and version", diagnostic);
        }
        return true;
    }

    [[nodiscard]] bool fail(
        const Token& token,
        std::string_view message,
        std::string& diagnostic) const {
        std::ostringstream out;
        out << source_name_ << ':' << token.line << ':' << token.column << ": " << message;
        diagnostic = out.str();
        return false;
    }
};

[[nodiscard]] std::uint64_t fnv1a64(std::string_view source) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const char raw_ch : source) {
        const auto ch = static_cast<unsigned char>(raw_ch);
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] std::string hex_u64(std::uint64_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto shift = static_cast<unsigned int>((result.size() - 1U - i) * 4U);
        result[i] = digits[(value >> shift) & 0xFU];
    }
    return result;
}

[[nodiscard]] std::string cpp_namespace(std::string_view package) {
    std::string result;
    for (const char ch : package) {
        if (ch == '.') {
            result += "::";
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

[[nodiscard]] std::string snake_case(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (std::isupper(static_cast<unsigned char>(ch)) != 0) {
            if (i != 0U) {
                result.push_back('_');
            }
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string result;
    for (const char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

[[nodiscard]] std::string type_cpp(const FieldType& type) {
    switch (type.kind) {
    case FieldTypeKind::u32: return "std::uint32_t";
    case FieldTypeKind::u64: return "std::uint64_t";
    case FieldTypeKind::string: return "std::string_view";
    }
    return "void";
}

[[nodiscard]] std::string type_name(const FieldType& type) {
    switch (type.kind) {
    case FieldTypeKind::u32: return "u32";
    case FieldTypeKind::u64: return "u64";
    case FieldTypeKind::string: return "string";
    }
    return "unknown";
}

[[nodiscard]] std::uint64_t field_max_wire_size(const FieldDecl& field) noexcept {
    switch (field.type.kind) {
    case FieldTypeKind::u32: return 4;
    case FieldTypeKind::u64: return 8;
    case FieldTypeKind::string:
        return 4ULL + static_cast<std::uint64_t>(field.type.string_maximum);
    }
    return 0;
}

[[nodiscard]] std::uint64_t struct_max_wire_size(const StructDecl& structure) noexcept {
    std::uint64_t total = 4ULL;
    for (const auto& field : structure.fields) {
        total += field_max_wire_size(field);
    }
    return total;
}

[[nodiscard]] const StructDecl& find_struct(const Schema& schema, std::string_view name) {
    for (const auto& structure : schema.structs) {
        if (structure.name == name) {
            return structure;
        }
    }
    std::abort();
}

[[nodiscard]] bool validate_schema(const Schema& schema, std::string& diagnostic, std::string_view source_name) {
    std::unordered_set<std::string> type_names;
    std::unordered_map<std::string, const StructDecl*> structs;

    for (const auto& structure : schema.structs) {
        if (!type_names.insert(structure.name).second) {
            std::ostringstream out;
            out << source_name << ':' << structure.line << ':' << structure.column
                << ": duplicate struct '" << structure.name << "'";
            diagnostic = out.str();
            return false;
        }
        structs.emplace(structure.name, &structure);

        std::unordered_set<std::string> field_names;
        std::unordered_set<std::uint32_t> field_ids;
        std::uint32_t previous_id = 0;
        std::uint64_t maximum_body = 0;
        for (const auto& field : structure.fields) {
            if (!field_names.insert(field.name).second) {
                std::ostringstream out;
                out << source_name << ':' << field.line << ':' << field.column
                    << ": duplicate field name '" << field.name << "'";
                diagnostic = out.str();
                return false;
            }
            if (!field_ids.insert(field.id).second) {
                std::ostringstream out;
                out << source_name << ':' << field.line << ':' << field.column
                    << ": duplicate field id " << field.id;
                diagnostic = out.str();
                return false;
            }
            if (field.id <= previous_id) {
                std::ostringstream out;
                out << source_name << ':' << field.line << ':' << field.column
                    << ": M0 fields must use strictly increasing ids (append-only wire order)";
                diagnostic = out.str();
                return false;
            }
            previous_id = field.id;
            maximum_body += field_max_wire_size(field);
            if (maximum_body + 4ULL > kMaxInlinePayload) {
                std::ostringstream out;
                out << source_name << ':' << structure.line << ':' << structure.column
                    << ": maximum encoded size of struct '" << structure.name
                    << "' exceeds 64 KiB inline payload limit";
                diagnostic = out.str();
                return false;
            }
        }
    }

    const auto& service = *schema.service;
    if (type_names.contains(service.name)) {
        std::ostringstream out;
        out << source_name << ':' << service.line << ':' << service.column
            << ": service name conflicts with struct name '" << service.name << "'";
        diagnostic = out.str();
        return false;
    }

    std::unordered_set<std::string> operation_names;
    std::unordered_set<std::uint32_t> operation_ids;
    for (const auto& operation : service.operations) {
        if (!operation_names.insert(operation.name).second) {
            std::ostringstream out;
            out << source_name << ':' << operation.line << ':' << operation.column
                << ": duplicate operation name '" << operation.name << "'";
            diagnostic = out.str();
            return false;
        }
        if (!operation_ids.insert(operation.id).second) {
            std::ostringstream out;
            out << source_name << ':' << operation.line << ':' << operation.column
                << ": duplicate operation id " << operation.id;
            diagnostic = out.str();
            return false;
        }
        if (!structs.contains(operation.request_type)) {
            std::ostringstream out;
            out << source_name << ':' << operation.line << ':' << operation.column
                << ": unknown request type '" << operation.request_type << "'";
            diagnostic = out.str();
            return false;
        }
        if (!structs.contains(operation.response_type)) {
            std::ostringstream out;
            out << source_name << ':' << operation.line << ':' << operation.column
                << ": unknown response type '" << operation.response_type << "'";
            diagnostic = out.str();
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::string generated_banner(std::string_view source_hash) {
    std::ostringstream out;
    out << "// Generated by osidlc " << compiler_version << ". DO NOT EDIT.\n"
        << "// Source hash (diagnostic FNV-1a-64, not a security primitive): " << source_hash << "\n"
        << "// Wire schema: " << wire_schema << "\n\n";
    return out.str();
}

[[nodiscard]] std::string generate_types(const Schema& schema, std::string_view hash) {
    const auto ns = cpp_namespace(schema.package);
    std::ostringstream out;
    out << generated_banner(hash)
        << "#pragma once\n\n"
        << "#include <cstdint>\n"
        << "#include <string_view>\n\n"
        << "namespace " << ns << " {\n\n";
    for (const auto& structure : schema.structs) {
        out << "struct " << structure.name << " final {\n";
        for (const auto& field : structure.fields) {
            out << "    " << type_cpp(field.type) << ' ' << field.name << " {};\n";
        }
        out << "};\n\n";
    }
    out << "} // namespace " << ns << "\n";
    return out.str();
}

[[nodiscard]] std::string generate_codec_header(const Schema& schema, std::string_view hash) {
    const auto ns = cpp_namespace(schema.package);
    std::ostringstream out;
    out << generated_banner(hash)
        << "#pragma once\n\n"
        << "#include <os/core/result.hpp>\n"
        << "#include <os/ipc/decoder.hpp>\n"
        << "#include <os/ipc/encoder.hpp>\n\n"
        << "#include \"" << snake_case(schema.service->name) << "_types.generated.hpp\"\n\n"
        << "namespace " << ns << " {\n\n";
    for (const auto& structure : schema.structs) {
        const auto function_name = snake_case(structure.name);
        out << "[[nodiscard]] os::core::Result<void> encode_" << function_name
            << "(os::ipc::Encoder& encoder, const " << structure.name << "& value) noexcept;\n";
        out << "[[nodiscard]] os::core::Result<" << structure.name << "> decode_" << function_name
            << "(os::ipc::Decoder& decoder) noexcept;\n\n";
    }
    out << "} // namespace " << ns << "\n";
    return out.str();
}

void emit_size_calculation(std::ostringstream& out, const StructDecl& structure) {
    out << "    std::size_t body_size = 0;\n";
    for (const auto& field : structure.fields) {
        if (field.type.kind == FieldTypeKind::string) {
            out << "    if (value." << field.name << ".size() > " << field.type.string_maximum << "U) {\n"
                << "        return ipc_error(os::ipc::errors::oversized_message);\n"
                << "    }\n"
                << "    auto " << field.name << "_size = os::core::checked_add(sizeof(std::uint32_t), value."
                << field.name << ".size());\n"
                << "    if (!" << field.name << "_size) {\n"
                << "        return ipc_error(os::ipc::errors::oversized_message);\n"
                << "    }\n"
                << "    auto " << field.name << "_body = os::core::checked_add(body_size, " << field.name << "_size.value());\n"
                << "    if (!" << field.name << "_body) {\n"
                << "        return ipc_error(os::ipc::errors::oversized_message);\n"
                << "    }\n"
                << "    body_size = " << field.name << "_body.value();\n";
        } else {
            const auto size = field.type.kind == FieldTypeKind::u32 ? 4U : 8U;
            out << "    auto " << field.name << "_body = os::core::checked_add(body_size, " << size << "U);\n"
                << "    if (!" << field.name << "_body) {\n"
                << "        return ipc_error(os::ipc::errors::oversized_message);\n"
                << "    }\n"
                << "    body_size = " << field.name << "_body.value();\n";
        }
    }
    out << "    auto total_size = os::core::checked_add(sizeof(std::uint32_t), body_size);\n"
        << "    if (!total_size || total_size.value() > os::ipc::max_inline_payload_size) {\n"
        << "        return ipc_error(os::ipc::errors::oversized_message);\n"
        << "    }\n"
        << "    if (encoder.remaining() < total_size.value()) {\n"
        << "        return ipc_error(os::ipc::errors::buffer_too_small);\n"
        << "    }\n";
}

[[nodiscard]] std::string generate_codec_source(const Schema& schema, std::string_view hash) {
    const auto ns = cpp_namespace(schema.package);
    std::ostringstream out;
    out << generated_banner(hash)
        << "#include \"" << snake_case(schema.service->name) << "_codec.generated.hpp\"\n\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n"
        << "#include <limits>\n\n"
        << "#include <os/core/checked.hpp>\n"
        << "#include <os/core/error.hpp>\n"
        << "#include <os/ipc/constants.hpp>\n\n"
        << "namespace " << ns << " {\n"
        << "namespace {\n\n"
        << "[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {\n"
        << "    return os::core::make_error(os::core::ErrorDomain::ipc, code);\n"
        << "}\n\n"
        << "} // namespace\n\n";

    for (const auto& structure : schema.structs) {
        const auto function_name = snake_case(structure.name);
        out << "os::core::Result<void> encode_" << function_name
            << "(os::ipc::Encoder& encoder, const " << structure.name << "& value) noexcept {\n";
        if (structure.fields.empty()) {
            out << "    (void)value;\n";
        }
        emit_size_calculation(out, structure);
        out << "    auto result = encoder.write_u32_le(static_cast<std::uint32_t>(body_size));\n"
            << "    if (!result) {\n"
            << "        return result.error();\n"
            << "    }\n";
        for (const auto& field : structure.fields) {
            switch (field.type.kind) {
            case FieldTypeKind::u32:
                out << "    result = encoder.write_u32_le(value." << field.name << ");\n";
                break;
            case FieldTypeKind::u64:
                out << "    result = encoder.write_u64_le(value." << field.name << ");\n";
                break;
            case FieldTypeKind::string:
                out << "    result = encoder.write_utf8(value." << field.name << ", "
                    << field.type.string_maximum << "U);\n";
                break;
            }
            out << "    if (!result) {\n"
                << "        return result.error();\n"
                << "    }\n";
        }
        out << "    return {};\n"
            << "}\n\n";

        out << "os::core::Result<" << structure.name << "> decode_" << function_name
            << "(os::ipc::Decoder& decoder) noexcept {\n"
            << "    auto body_size_result = decoder.read_u32_le();\n"
            << "    if (!body_size_result) {\n"
            << "        return body_size_result.error();\n"
            << "    }\n"
            << "    if (body_size_result.value() > os::ipc::max_inline_payload_size) {\n"
            << "        return ipc_error(os::ipc::errors::oversized_message);\n"
            << "    }\n"
            << "    auto body_result = decoder.read_raw(static_cast<std::size_t>(body_size_result.value()));\n"
            << "    if (!body_result) {\n"
            << "        return body_result.error();\n"
            << "    }\n"
            << "    auto end_result = decoder.require_end();\n"
            << "    if (!end_result) {\n"
            << "        return end_result.error();\n"
            << "    }\n"
            << "    os::ipc::Decoder body{body_result.value()};\n"
            << "    " << structure.name << " value{};\n";

        for (const auto& field : structure.fields) {
            out << "    if (body.remaining() != 0U) {\n";
            switch (field.type.kind) {
            case FieldTypeKind::u32:
                out << "        auto field_result = body.read_u32_le();\n";
                break;
            case FieldTypeKind::u64:
                out << "        auto field_result = body.read_u64_le();\n";
                break;
            case FieldTypeKind::string:
                out << "        auto field_result = body.read_utf8(" << field.type.string_maximum << "U);\n";
                break;
            }
            out << "        if (!field_result) {\n"
                << "            return field_result.error();\n"
                << "        }\n"
                << "        value." << field.name << " = field_result.value();\n"
                << "    }\n";
        }
        out << "    return value;\n"
            << "}\n\n";
    }

    out << "} // namespace " << ns << "\n";
    return out.str();
}

[[nodiscard]] std::string generate_client_header(const Schema& schema, std::string_view hash) {
    const auto ns = cpp_namespace(schema.package);
    const auto& service = *schema.service;
    std::ostringstream out;
    out << generated_banner(hash)
        << "#pragma once\n\n"
        << "#include <array>\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n"
        << "#include <utility>\n\n"
        << "#include <os/core/span.hpp>\n"
        << "#include <os/core/strong_id.hpp>\n"
        << "#include <os/ipc/encoder.hpp>\n"
        << "#include <os/ipc/rpc.hpp>\n\n"
        << "#include \"" << snake_case(schema.service->name) << "_codec.generated.hpp\"\n"
        << "#include \"" << snake_case(schema.service->name) << "_types.generated.hpp\"\n\n"
        << "namespace " << ns << " {\n\n"
        << "class " << service.name << "Client final {\n"
        << "public:\n"
        << "    explicit " << service.name << "Client(os::ipc::ClientConnection& connection) noexcept\n"
        << "        : connection_(&connection) {}\n\n"
        << "    static constexpr os::core::ServiceId service_id{0x" << std::hex << service.id << std::dec << "U};\n"
        << "    static constexpr std::uint16_t service_version{" << service.version << "U};\n";
    for (const auto& operation : service.operations) {
        out << "    static constexpr std::uint32_t " << snake_case(operation.name)
            << "_operation_id{" << operation.id << "U};\n";
    }
    out << "\n    [[nodiscard]] os::ipc::ClientConnection& connection() const noexcept { return *connection_; }\n\n";
    for (const auto& operation : service.operations) {
        const auto& request_struct = find_struct(schema, operation.request_type);
        const auto max_request = struct_max_wire_size(request_struct);
        out << "    [[nodiscard]] os::core::Result<" << operation.response_type << "> "
            << snake_case(operation.name) << "(\n"
            << "        const " << operation.request_type << "& request,\n"
            << "        os::core::MutableByteSpan response_packet_buffer) noexcept {\n"
            << "        std::array<std::byte, " << max_request << "U> request_storage{};\n"
            << "        os::ipc::Encoder encoder{request_storage};\n"
            << "        auto encode_result = encode_" << snake_case(operation.request_type) << "(encoder, request);\n"
            << "        if (!encode_result) {\n"
            << "            return encode_result.error();\n"
            << "        }\n"
            << "        auto call_result = connection_->call(\n"
            << "            service_id, " << snake_case(operation.name) << "_operation_id, encoder.written(), response_packet_buffer);\n"
            << "        if (!call_result) {\n"
            << "            return call_result.error();\n"
            << "        }\n"
            << "        auto inbound = std::move(call_result).value();\n"
            << "        os::ipc::Decoder decoder{inbound.payload()};\n"
            << "        return decode_" << snake_case(operation.response_type) << "(decoder);\n"
            << "    }\n\n";
    }
    out << "private:\n"
        << "    os::ipc::ClientConnection* connection_;\n"
        << "};\n\n"
        << "} // namespace " << ns << "\n";
    return out.str();
}

[[nodiscard]] std::string generate_server_header(const Schema& schema, std::string_view hash) {
    const auto ns = cpp_namespace(schema.package);
    const auto& service = *schema.service;
    std::ostringstream out;
    out << generated_banner(hash)
        << "#pragma once\n\n"
        << "#include <array>\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n"
        << "#include <utility>\n\n"
        << "#include <os/core/error.hpp>\n"
        << "#include <os/core/result.hpp>\n"
        << "#include <os/ipc/decoder.hpp>\n"
        << "#include <os/ipc/encoder.hpp>\n"
        << "#include <os/ipc/rpc.hpp>\n\n"
        << "#include \"" << snake_case(schema.service->name) << "_codec.generated.hpp\"\n"
        << "#include \"" << snake_case(schema.service->name) << "_types.generated.hpp\"\n\n"
        << "namespace " << ns << " {\n\n"
        << "class " << service.name << "Server {\n"
        << "public:\n"
        << "    virtual ~" << service.name << "Server() = default;\n\n";
    for (const auto& operation : service.operations) {
        out << "    [[nodiscard]] virtual os::core::Result<" << operation.response_type << "> "
            << snake_case(operation.name) << "(const os::ipc::RequestContext& context, const "
            << operation.request_type << "& request) noexcept = 0;\n";
    }
    out << "};\n\n"
        << "class " << service.name << "Dispatcher final {\n"
        << "public:\n"
        << "    " << service.name << "Dispatcher(\n"
        << "        " << service.name << "Server& implementation,\n"
        << "        os::ipc::Channel& channel,\n"
        << "        os::ipc::PeerIdentityResolver& identity_resolver) noexcept\n"
        << "        : implementation_(&implementation), channel_(&channel), identity_resolver_(&identity_resolver) {}\n\n"
        << "    [[nodiscard]] os::core::Result<void> dispatch_one(os::core::MutableByteSpan receive_buffer) noexcept {\n"
        << "        auto receive_result = channel_->receive(receive_buffer);\n"
        << "        if (!receive_result) {\n"
        << "            return receive_result.error();\n"
        << "        }\n"
        << "        auto message = std::move(receive_result).value();\n"
        << "        auto context_result = os::ipc::validate_rpc_request(message, service_id, *identity_resolver_);\n"
        << "        if (!context_result) {\n"
        << "            return os::ipc::send_rpc_error(*channel_, message.header(), context_result.error());\n"
        << "        }\n"
        << "        const auto context = context_result.value();\n"
        << "        switch (message.header().operation_id) {\n";
    for (const auto& operation : service.operations) {
        const auto& response_struct = find_struct(schema, operation.response_type);
        const auto max_response = struct_max_wire_size(response_struct);
        out << "        case " << operation.id << "U: {\n"
            << "            os::ipc::Decoder decoder{message.payload()};\n"
            << "            auto request_result = decode_" << snake_case(operation.request_type) << "(decoder);\n"
            << "            if (!request_result) {\n"
            << "                return os::ipc::send_rpc_error(*channel_, message.header(), request_result.error());\n"
            << "            }\n"
            << "            auto response_result = implementation_->" << snake_case(operation.name)
            << "(context, request_result.value());\n"
            << "            if (!response_result) {\n"
            << "                return os::ipc::send_rpc_error(*channel_, message.header(), response_result.error());\n"
            << "            }\n"
            << "            std::array<std::byte, " << max_response << "U> response_storage{};\n"
            << "            os::ipc::Encoder encoder{response_storage};\n"
            << "            auto encode_result = encode_" << snake_case(operation.response_type)
            << "(encoder, response_result.value());\n"
            << "            if (!encode_result) {\n"
            << "                return os::ipc::send_rpc_error(*channel_, message.header(), encode_result.error());\n"
            << "            }\n"
            << "            return os::ipc::send_rpc_response(*channel_, message.header(), encoder.written());\n"
            << "        }\n";
    }
    out << "        default:\n"
        << "            return os::ipc::send_rpc_error(\n"
        << "                *channel_, message.header(),\n"
        << "                os::core::make_error(os::core::ErrorDomain::service, os::core::errors::service::unknown_operation));\n"
        << "        }\n"
        << "    }\n\n"
        << "    static constexpr os::core::ServiceId service_id{0x" << std::hex << service.id << std::dec << "U};\n\n"
        << "private:\n"
        << "    " << service.name << "Server* implementation_;\n"
        << "    os::ipc::Channel* channel_;\n"
        << "    os::ipc::PeerIdentityResolver* identity_resolver_;\n"
        << "};\n\n"
        << "} // namespace " << ns << "\n";
    return out.str();
}

[[nodiscard]] std::string generate_abi_json(const Schema& schema, std::string_view hash) {
    const auto& service = *schema.service;
    std::ostringstream out;
    out << "{\n"
        << "  \"compiler\": \"osidlc " << compiler_version << "\",\n"
        << "  \"source_hash\": \"fnv1a64:" << hash << "\",\n"
        << "  \"wire_schema\": \"" << wire_schema << "\",\n"
        << "  \"package\": \"" << json_escape(schema.package) << "\",\n"
        << "  \"structs\": [\n";
    for (std::size_t i = 0; i < schema.structs.size(); ++i) {
        const auto& structure = schema.structs[i];
        out << "    {\"name\": \"" << json_escape(structure.name) << "\", \"fields\": [";
        for (std::size_t j = 0; j < structure.fields.size(); ++j) {
            const auto& field = structure.fields[j];
            out << "{\"id\": " << field.id << ", \"name\": \"" << json_escape(field.name)
                << "\", \"type\": \"" << type_name(field.type) << "\"";
            if (field.type.kind == FieldTypeKind::string) {
                out << ", \"maximum\": " << field.type.string_maximum;
            }
            out << '}';
            if (j + 1U != structure.fields.size()) {
                out << ", ";
            }
        }
        out << "]}";
        if (i + 1U != schema.structs.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n"
        << "  \"service\": {\n"
        << "    \"name\": \"" << json_escape(service.name) << "\",\n"
        << "    \"id\": " << service.id << ",\n"
        << "    \"version\": " << service.version << ",\n"
        << "    \"operations\": [\n";
    for (std::size_t i = 0; i < service.operations.size(); ++i) {
        const auto& operation = service.operations[i];
        out << "      {\"id\": " << operation.id << ", \"name\": \""
            << json_escape(operation.name) << "\", \"request\": \""
            << json_escape(operation.request_type) << "\", \"response\": \""
            << json_escape(operation.response_type) << "\"}";
        if (i + 1U != service.operations.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

[[nodiscard]] GeneratedFiles generate_files(const Schema& schema, std::string_view source_hash) {
    return GeneratedFiles{
        .basename = snake_case(schema.service->name),
        .types_header = generate_types(schema, source_hash),
        .codec_header = generate_codec_header(schema, source_hash),
        .codec_source = generate_codec_source(schema, source_hash),
        .client_header = generate_client_header(schema, source_hash),
        .server_header = generate_server_header(schema, source_hash),
        .abi_json = generate_abi_json(schema, source_hash),
    };
}

[[nodiscard]] bool write_file(
    const std::filesystem::path& path,
    std::string_view contents,
    std::string& diagnostic) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        diagnostic = "osidlc: cannot open output file: " + path.string();
        return false;
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        diagnostic = "osidlc: failed writing output file: " + path.string();
        return false;
    }
    return true;
}

} // namespace

CompileResult compile_text(std::string_view source, std::string_view source_name) {
    std::string diagnostic;
    Lexer lexer{source, source_name};
    auto tokens = lexer.lex(diagnostic);
    if (!diagnostic.empty()) {
        return CompileResult{false, {}, std::move(diagnostic)};
    }

    Schema schema;
    Parser parser{std::move(tokens), source_name};
    if (!parser.parse(schema, diagnostic)) {
        return CompileResult{false, {}, std::move(diagnostic)};
    }
    if (!validate_schema(schema, diagnostic, source_name)) {
        return CompileResult{false, {}, std::move(diagnostic)};
    }

    const auto source_hash = hex_u64(fnv1a64(source));
    return CompileResult{true, generate_files(schema, source_hash), {}};
}

bool compile_file(
    const std::filesystem::path& input,
    const std::filesystem::path& output_directory,
    std::string& diagnostic) {
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        diagnostic = "osidlc: cannot open input file: " + input.string();
        return false;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        diagnostic = "osidlc: failed reading input file: " + input.string();
        return false;
    }

    auto result = compile_text(contents.str(), input.string());
    if (!result.success) {
        diagnostic = std::move(result.diagnostic);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        diagnostic = "osidlc: cannot create output directory: " + output_directory.string() + ": " + ec.message();
        return false;
    }

    const auto& base = result.files.basename;

    const auto write = [&](std::string_view suffix, std::string_view data) {
        return write_file(output_directory / (base + std::string{suffix}), data, diagnostic);
    };

    return write("_types.generated.hpp", result.files.types_header) &&
        write("_codec.generated.hpp", result.files.codec_header) &&
        write("_codec.generated.cpp", result.files.codec_source) &&
        write("_client.generated.hpp", result.files.client_header) &&
        write("_server.generated.hpp", result.files.server_header) &&
        write("_abi.generated.json", result.files.abi_json);
}

} // namespace osidlc
