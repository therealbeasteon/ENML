#include <iostream>
#include <string>

#include <osidlc/compiler.hpp>

namespace {

void print_usage(const char* executable) {
    std::cerr << "usage: " << executable
              << " --input <file.osidl> --out-dir <directory>\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--input" && i + 1 < argc) {
            input = argv[++i];
        } else if (argument == "--out-dir" && i + 1 < argc) {
            output = argv[++i];
        } else if (argument == "--version") {
            std::cout << "osidlc " << osidlc::compiler_version << '\n';
            return 0;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (input.empty() || output.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    std::string diagnostic;
    if (!osidlc::compile_file(input, output, diagnostic)) {
        std::cerr << diagnostic << '\n';
        return 1;
    }

    return 0;
}
