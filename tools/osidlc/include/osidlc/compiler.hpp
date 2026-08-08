#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace osidlc {

inline constexpr std::string_view compiler_version = "0.3.0-m0.7";
inline constexpr std::string_view wire_schema = "appendable-positional-v0";

struct GeneratedFiles final {
    std::string basename;
    std::string types_header;
    std::string codec_header;
    std::string codec_source;
    std::string client_header;
    std::string server_header;
    std::string abi_json;
};

struct CompileResult final {
    bool success {false};
    GeneratedFiles files {};
    std::string diagnostic {};
};

[[nodiscard]] CompileResult compile_text(
    std::string_view source,
    std::string_view source_name = "<memory>");

[[nodiscard]] bool compile_file(
    const std::filesystem::path& input,
    const std::filesystem::path& output_directory,
    std::string& diagnostic);

} // namespace osidlc
