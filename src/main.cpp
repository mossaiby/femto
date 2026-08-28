#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

#include "common/arena.hpp"
#include "common/source_manager.hpp"
#include "common/diagnostic.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/type_checker.hpp"
#include "codegen/nasm_emitter.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: femtoc <source.femt> [-o output]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = "a.out";

    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    std::ifstream file(input_path);
    if (!file.is_open()) {
        std::cerr << "error: failed to open file '" << input_path << "'\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    femto::Arena arena;
    femto::SourceManager sm(input_path, source);
    femto::Diagnostics diag(sm);
    femto::Lexer lexer(sm, diag);
    femto::Parser parser(lexer, arena, diag);

    femto::ASTProgram prog = parser.parse_program();
    if (diag.has_errors()) {
        std::cerr << "Compilation aborted due to syntax errors.\n";
        return 1;
    }

    femto::TypeChecker checker(diag);
    if (!checker.check_program(prog)) {
        std::cerr << "Compilation aborted due to semantic errors.\n";
        return 1;
    }

    femto::NasmEmitter emitter(checker.type_env());
    std::string asm_code = emitter.generate_assembly(prog);

    std::string asm_path = input_path + ".asm";
    std::string obj_path = input_path + ".o";

    std::ofstream asm_out(asm_path);
    asm_out << asm_code;
    asm_out.close();

    std::string nasm_cmd = "nasm -f elf64 " + asm_path + " -o " + obj_path;
    if (std::system(nasm_cmd.c_str()) != 0) {
        std::cerr << "error: nasm assembly failed\n";
        return 1;
    }

    std::string link_cmd = "gcc -no-pie " + obj_path + " -o " + output_path;
    if (std::system(link_cmd.c_str()) != 0) {
        std::cerr << "error: linking failed\n";
        return 1;
    }

    std::cout << "Build successful: " << output_path << "\n";
    return 0;
}