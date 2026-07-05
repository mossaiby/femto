#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/diagnostic.h"
#include "lexer/lexer.h"

static void print_usage(const char* prog) {
    std::fprintf(stderr, "Usage: %s <source.femto>\n", prog);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::filesystem::path source_path = argv[1];

    if (!std::filesystem::exists(source_path)) {
        std::fprintf(stderr, "error: file not found: %s\n", source_path.c_str());
        return 1;
    }

    std::ifstream file(source_path);
    if (!file.is_open()) {
        std::fprintf(stderr, "error: cannot open: %s\n", source_path.c_str());
        return 1;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    femto::DiagnosticEngine diag(source_path.string(), source);
    femto::Lexer lexer(source, diag);

    auto tokens = lexer.tokenize();

    for (const auto& tok : tokens) {
        std::printf("%s\n", tok.to_string().c_str());
    }

    if (diag.has_errors()) {
        diag.print_all();
        return 1;
    }

    return 0;
}
