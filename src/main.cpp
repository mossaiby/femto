#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <cstdlib>
#include <cstring>

#include "common/arena.hpp"
#include "common/source_manager.hpp"
#include "common/diagnostic.hpp"
#include "common/module_loader.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/type_checker.hpp"
#include "codegen/nasm_emitter.hpp"
#include "lsp/lsp_server.hpp"

#ifndef FEMTO_RUNTIME_OBJ
#define FEMTO_RUNTIME_OBJ "runtime/femto_rt.o"
#endif

#ifndef FEMTO_STDLIB_DIR
#define FEMTO_STDLIB_DIR "stdlib"
#endif

namespace fs = std::filesystem;

static fs::path get_executable_dir(const char* argv0) {
    try {
        fs::path p = fs::canonical("/proc/self/exe");
        return p.parent_path();
    } catch (...) {
        try {
            return fs::canonical(argv0).parent_path();
        } catch (...) {
            return fs::current_path();
        }
    }
}

int main(int argc, char** argv) {
    std::string primary_input;
    std::string output_path = "a.out";
    std::string stdlib_dir;
    bool keep_temps = false;
    bool enable_bounds_checks = true;
    bool is_lsp_mode = false;
    std::vector<std::string> search_paths = { "." };

    fs::path exe_dir = get_executable_dir(argv[0]);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lsp") {
            is_lsp_mode = true;
        } else if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            search_paths.push_back(argv[++i]);
        } else if (arg.rfind("-I", 0) == 0) {
            search_paths.push_back(arg.substr(2));
        } else if (arg == "--stdlib" && i + 1 < argc) {
            stdlib_dir = argv[++i];
        } else if (arg == "--no-bounds-check") {
            enable_bounds_checks = false;
        } else if (arg == "--keep-temps" || arg == "-k") {
            keep_temps = true;
        } else {
            primary_input = arg;
        }
    }

    // Resolve standard library directory
    if (stdlib_dir.empty()) {
        fs::path default_stdlib = exe_dir / "stdlib";
        if (fs::exists(default_stdlib)) {
            stdlib_dir = default_stdlib.string();
        } else if (fs::exists(FEMTO_STDLIB_DIR)) {
            stdlib_dir = FEMTO_STDLIB_DIR;
        } else if (fs::exists(exe_dir.parent_path() / "stdlib")) {
            stdlib_dir = (exe_dir.parent_path() / "stdlib").string();
        }
    }

    if (!stdlib_dir.empty() && fs::exists(stdlib_dir)) {
        search_paths.push_back(stdlib_dir);
    }

    // Run in Language Server Mode if requested
    if (is_lsp_mode) {
        femto::lsp::LspServer server;
        server.set_search_paths(search_paths);
        server.run();
        return 0;
    }

    if (primary_input.empty()) {
        std::cerr << "Usage: femtoc <source.femto> [options]\n"
                  << "Options:\n"
                  << "  -o <file>             Specify output executable name (default: a.out)\n"
                  << "  -I <dir>              Add search directory for imports\n"
                  << "  --stdlib <dir>        Specify the path to the standard library\n"
                  << "  --lsp                 Start the Language Server Protocol (LSP) daemon\n"
                  << "  --no-bounds-check     Disable runtime array and slice bounds checks\n"
                  << "  --keep-temps, -k      Keep intermediate assembly (.asm) and object (.o) files\n";
        return 1;
    }

    if (stdlib_dir.empty() || !fs::exists(stdlib_dir)) {
        std::cerr << "error: stdlib directory not found. Please specify using --stdlib <path>\n";
        return 1;
    }

    femto::Arena arena;
    femto::SourceManager dummy_sm(primary_input, "");
    femto::Diagnostics diag(dummy_sm);

    femto::ModuleLoader loader(search_paths, arena);
    femto::ASTProgram master_prog;

    if (!loader.load_module_recursive(primary_input, "", master_prog, diag)) {
        std::cerr << "Compilation failed during module loading.\n";
        return 1;
    }

    femto::TypeChecker checker(arena, diag);
    if (!checker.check_program(master_prog)) {
        std::cerr << "Compilation aborted due to semantic errors.\n";
        return 1;
    }

    femto::NasmEmitter emitter(checker.type_env(), checker.enum_defs(), checker.const_defs(), checker.float_const_defs(), enable_bounds_checks);
    std::string asm_code = emitter.generate_assembly(master_prog);

    std::string asm_path = output_path + ".asm";
    std::string obj_path = output_path + ".o";

    std::ofstream asm_out(asm_path);
    asm_out << asm_code;
    asm_out.close();

    // Assemble with NASM
    std::string nasm_cmd = "nasm -f elf64 " + asm_path + " -o " + obj_path;
    if (std::system(nasm_cmd.c_str()) != 0) {
        std::cerr << "error: nasm assembly failed\n";
        if (!keep_temps) {
            fs::remove(asm_path);
            fs::remove(obj_path);
        }
        return 1;
    }

    // Resolve runtime object or assemble on demand
    std::string rt_obj = FEMTO_RUNTIME_OBJ;
    if (!fs::exists(rt_obj)) {
        if (fs::exists(exe_dir / "femto_rt.o")) {
            rt_obj = (exe_dir / "femto_rt.o").string();
        } else if (fs::exists(exe_dir / "runtime" / "femto_rt.o")) {
            rt_obj = (exe_dir / "runtime" / "femto_rt.o").string();
        } else {
            fs::path rt_asm = fs::path(stdlib_dir).parent_path() / "runtime" / "femto_rt.asm";
            if (fs::exists(rt_asm)) {
                fs::path target_obj = exe_dir / "femto_rt.o";
                std::string assemble_rt_cmd = "nasm -f elf64 " + rt_asm.string() + " -o " + target_obj.string();
                if (std::system(assemble_rt_cmd.c_str()) == 0) {
                    rt_obj = target_obj.string();
                }
            }
        }
    }

    // Link with gcc, runtime object, and libm (-lm)
    std::string link_cmd = "gcc -no-pie " + obj_path + " " + rt_obj + " -lm -o " + output_path;
    if (std::system(link_cmd.c_str()) != 0) {
        std::cerr << "error: linking failed (make sure runtime/femto_rt.o is assembled)\n";
        if (!keep_temps) {
            fs::remove(asm_path);
            fs::remove(obj_path);
        }
        return 1;
    }

    if (!keep_temps) {
        fs::remove(asm_path);
        fs::remove(obj_path);
    }

    std::cout << "Build successful: " << output_path << "\n";
    return 0;
}