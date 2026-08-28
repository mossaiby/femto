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
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/type_checker.hpp"
#include "codegen/nasm_emitter.hpp"

#ifndef FEMTO_RUNTIME_OBJ
#define FEMTO_RUNTIME_OBJ "runtime/femto_rt.o"
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

class ModuleLoader {
public:
    ModuleLoader(const std::vector<std::string>& search_paths, femto::Arena& arena)
        : search_paths_(search_paths), arena_(arena) {}

    bool load_module_recursive(const std::string& filepath, const std::string& mod_prefix, femto::ASTProgram& master_prog, femto::Diagnostics& diag) {
        std::string canonical_path;
        try {
            if (!fs::exists(filepath)) {
                std::cerr << "error: file not found: '" << filepath << "'\n";
                return false;
            }
            canonical_path = fs::canonical(filepath).string();
        } catch (...) {
            canonical_path = filepath;
        }

        if (visited_files_.count(canonical_path)) {
            return true;
        }
        visited_files_.insert(canonical_path);

        std::ifstream file(canonical_path);
        if (!file.is_open()) {
            std::cerr << "error: failed to open '" << canonical_path << "'\n";
            return false;
        }
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        auto* sm = arena_.allocate<femto::SourceManager>(canonical_path, source);
        femto::Lexer lexer(*sm, diag);
        femto::Parser parser(lexer, arena_, diag);

        femto::ASTProgram prog = parser.parse_program();
        if (diag.has_errors()) {
            return false;
        }

        // Recursively load imported modules
        for (const auto& imp : prog.imports) {
            std::string resolved_file = resolve_import_path(imp, canonical_path);
            if (resolved_file.empty()) {
                std::cerr << "error: cannot find module '" << imp << "' imported in '" << canonical_path << "'\n";
                return false;
            }
            // Module prefix for namespacing: "std/io" -> "std::io"
            std::string sub_prefix;
            for (char c : imp) {
                if (c == '/') {
                    sub_prefix += "::";
                } else {
                    sub_prefix += c;
                }
            }
            if (!load_module_recursive(resolved_file, sub_prefix, master_prog, diag)) {
                return false;
            }
        }

        // Merge declarations and register qualified names
        for (auto* fn : prog.functions) {
            master_prog.functions.push_back(fn);
            if (!mod_prefix.empty() && fn->name.find("::") == std::string_view::npos && !fn->is_extern_c) {
                std::string q_name = mod_prefix + "::" + std::string(fn->name);
                char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
                std::memcpy(q_buf, q_name.data(), q_name.size());
                q_buf[q_name.size()] = '\0';

                auto* alias_fn = arena_.allocate<femto::ASTFunctionDecl>(*fn);
                alias_fn->name = std::string_view(q_buf, q_name.size());
                master_prog.functions.push_back(alias_fn);
            }
        }
        for (auto* st : prog.structs)   master_prog.structs.push_back(st);
        for (auto* en : prog.enums)     master_prog.enums.push_back(en);
        for (auto* cn : prog.constants) master_prog.constants.push_back(cn);

        return true;
    }

private:
    std::string resolve_import_path(const std::string& mod_name, const std::string& current_file) {
        std::string rel_file = mod_name + ".femto";

        // 1. Check relative to importing file's directory
        fs::path cur_dir = fs::path(current_file).parent_path();
        if (fs::exists(cur_dir / rel_file)) {
            return (cur_dir / rel_file).string();
        }

        // 2. Check search paths
        for (const auto& sp : search_paths_) {
            fs::path p = fs::path(sp) / rel_file;
            if (fs::exists(p)) {
                return p.string();
            }
        }

        return "";
    }

    std::vector<std::string> search_paths_;
    std::unordered_set<std::string> visited_files_;
    femto::Arena& arena_;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: femtoc <source.femto> [options]\n"
                  << "Options:\n"
                  << "  -o <file>          Specify output executable name (default: a.out)\n"
                  << "  -I <dir>           Add search directory for imports\n"
                  << "  --stdlib <dir>     Specify the path to the standard library\n"
                  << "  --keep-temps, -k   Keep intermediate assembly (.asm) and object (.o) files\n";
        return 1;
    }

    std::string primary_input;
    std::string output_path = "a.out";
    std::string stdlib_dir;
    bool keep_temps = false;
    std::vector<std::string> search_paths = { "." };

    fs::path exe_dir = get_executable_dir(argv[0]);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            search_paths.push_back(argv[++i]);
        } else if (arg.rfind("-I", 0) == 0) {
            search_paths.push_back(arg.substr(2));
        } else if (arg == "--stdlib" && i + 1 < argc) {
            stdlib_dir = argv[++i];
        } else if (arg == "--keep-temps" || arg == "-k") {
            keep_temps = true;
        } else {
            primary_input = arg;
        }
    }

    if (primary_input.empty()) {
        std::cerr << "error: no input file specified\n";
        return 1;
    }

    // Resolve stdlib path strictly without heuristics
    if (stdlib_dir.empty()) {
        fs::path default_stdlib = exe_dir / "stdlib";
        if (fs::exists(default_stdlib)) {
            stdlib_dir = default_stdlib.string();
        } else {
            std::cerr << "error: stdlib directory not found at '" << default_stdlib.string()
                      << "'. Please specify the stdlib location using --stdlib <path>\n";
            return 1;
        }
    } else {
        if (!fs::exists(stdlib_dir)) {
            std::cerr << "error: specified stdlib directory does not exist: '" << stdlib_dir << "'\n";
            return 1;
        }
    }

    search_paths.push_back(stdlib_dir);

    femto::Arena arena;
    femto::SourceManager dummy_sm(primary_input, "");
    femto::Diagnostics diag(dummy_sm);

    ModuleLoader loader(search_paths, arena);
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

    femto::NasmEmitter emitter(checker.type_env(), checker.enum_defs(), checker.const_defs());
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

    std::string link_cmd = "gcc -no-pie " + obj_path + " " + rt_obj + " -o " + output_path;
    if (std::system(link_cmd.c_str()) != 0) {
        std::cerr << "error: linking failed (make sure runtime/femto_rt.o is assembled)\n";
        if (!keep_temps) {
            fs::remove(asm_path);
            fs::remove(obj_path);
        }
        return 1;
    }

    // Clean up intermediate files by default
    if (!keep_temps) {
        fs::remove(asm_path);
        fs::remove(obj_path);
    }

    std::cout << "Build successful: " << output_path << "\n";
    return 0;
}