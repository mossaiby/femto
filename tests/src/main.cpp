#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
#ifdef _WIN32
#define FEMTO_RUNTIME_OBJ "runtime/femto_rt.obj"
#else
#define FEMTO_RUNTIME_OBJ "runtime/femto_rt.o"
#endif
#endif

#ifndef FEMTO_STDLIB_DIR
#define FEMTO_STDLIB_DIR "stdlib"
#endif

namespace fs = std::filesystem;

static std::string quote(const std::string& path) {
    return "\"" + path + "\"";
}

static int run_command(const std::string& cmd) {
#ifdef _WIN32
    std::string wrapped = "\"" + cmd + "\"";
    return std::system(wrapped.c_str());
#else
    return std::system(cmd.c_str());
#endif
}

#ifdef _WIN32
struct MsvcToolchain {
    std::string link_exe;
    std::vector<std::string> lib_paths;
    bool is_valid = false;
};

static MsvcToolchain discover_msvc() {
    MsvcToolchain tc;

    // 1. Check if running in Developer Command Prompt
    const char* vc_dir = std::getenv("VCINSTALLDIR");
    if (vc_dir) {
        fs::path p = fs::path(vc_dir) / "bin" / "Hostx64" / "x64" / "link.exe";
        if (fs::exists(p)) {
            tc.link_exe = p.string();
            tc.is_valid = true;
            return tc;
        }
    }

    // 2. Scan standard Visual Studio installation directories
    std::vector<fs::path> vs_roots = {
        "C:\\Program Files\\Microsoft Visual Studio",
        "C:\\Program Files (x86)\\Microsoft Visual Studio"
    };

    for (const auto& root : vs_roots) {
        if (!fs::exists(root)) continue;
        try {
            for (const auto& year : fs::directory_iterator(root)) {
                if (!fs::is_directory(year)) continue;
                for (const auto& edition : fs::directory_iterator(year)) {
                    if (!fs::is_directory(edition)) continue;
                    fs::path msvc_tools = edition.path() / "VC" / "Tools" / "MSVC";
                    if (fs::exists(msvc_tools)) {
                        for (const auto& ver : fs::directory_iterator(msvc_tools)) {
                            fs::path link_p = ver.path() / "bin" / "Hostx64" / "x64" / "link.exe";
                            fs::path lib_p  = ver.path() / "lib" / "x64";
                            if (fs::exists(link_p) && fs::exists(lib_p)) {
                                tc.link_exe = link_p.string();
                                tc.lib_paths.push_back(lib_p.string());
                                tc.is_valid = true;
                                break;
                            }
                        }
                    }
                    if (tc.is_valid) break;
                }
                if (tc.is_valid) break;
            }
        } catch (...) {}
        if (tc.is_valid) break;
    }

    // 3. Scan Windows Kits for UCRT and UM libraries
    std::vector<fs::path> sdk_roots = {
        "C:\\Program Files (x86)\\Windows Kits\\10\\Lib",
        "C:\\Program Files\\Windows Kits\\10\\Lib"
    };
    for (const auto& sdk_root : sdk_roots) {
        if (!fs::exists(sdk_root)) continue;
        try {
            fs::path latest_ver;
            for (const auto& v : fs::directory_iterator(sdk_root)) {
                if (fs::is_directory(v) && fs::exists(v.path() / "ucrt" / "x64") && fs::exists(v.path() / "um" / "x64")) {
                    latest_ver = v.path();
                }
            }
            if (!latest_ver.empty()) {
                tc.lib_paths.push_back((latest_ver / "ucrt" / "x64").string());
                tc.lib_paths.push_back((latest_ver / "um" / "x64").string());
            }
        } catch (...) {}
    }

    return tc;
}
#endif

static fs::path get_executable_dir(const char* argv0) {
#ifdef _WIN32
    char buf[MAX_PATH];
    if (GetModuleFileNameA(NULL, buf, MAX_PATH) != 0) {
        return fs::path(buf).parent_path();
    }
    return fs::current_path();
#else
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
#endif
}

int main(int argc, char** argv) {
    std::string primary_input;
#ifdef _WIN32
    std::string output_path = "a.exe";
    femto::TargetOS target_os = femto::TargetOS::Windows;
#else
    std::string output_path = "a.out";
    femto::TargetOS target_os = femto::TargetOS::Linux;
#endif

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
        } else if (arg == "--target" && i + 1 < argc) {
            std::string tgt = argv[++i];
            if (tgt.find("windows") != std::string::npos || tgt.find("win64") != std::string::npos) {
                target_os = femto::TargetOS::Windows;
            } else {
                target_os = femto::TargetOS::Linux;
            }
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
        } else if (fs::exists(exe_dir.parent_path().parent_path() / "stdlib")) {
            stdlib_dir = (exe_dir.parent_path().parent_path() / "stdlib").string();
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
                  << "  -o <file>             Specify output executable name\n"
                  << "  --target <target>     Target platform (x86_64-linux, x86_64-windows)\n"
                  << "  -I <dir>              Add search directory for imports\n"
                  << "  --stdlib <dir>        Specify the path to the standard library\n"
                  << "  --lsp                 Start the Language Server Protocol (LSP) daemon\n"
                  << "  --no-bounds-check     Disable runtime array and slice bounds checks\n"
                  << "  --keep-temps, -k      Keep intermediate assembly (.asm) and object (.obj/.o) files\n";
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

    femto::NasmEmitter emitter(checker.type_env(), checker.enum_defs(), checker.const_defs(), checker.float_const_defs(), enable_bounds_checks, target_os);
    std::string asm_code = emitter.generate_assembly(master_prog);

    std::string asm_path = output_path + ".asm";
    std::string obj_ext = (target_os == femto::TargetOS::Windows) ? ".obj" : ".o";
    std::string obj_path = output_path + obj_ext;

    std::ofstream asm_out(asm_path);
    asm_out << asm_code;
    asm_out.close();

    // Assemble with NASM (paths quoted)
    std::string nasm_fmt = (target_os == femto::TargetOS::Windows) ? "win64" : "elf64";
    std::string nasm_cmd = "nasm -f " + nasm_fmt + " " + quote(asm_path) + " -o " + quote(obj_path);
    if (run_command(nasm_cmd) != 0) {
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
        std::string rt_obj_name = (target_os == femto::TargetOS::Windows) ? "femto_rt.obj" : "femto_rt.o";
        std::string rt_asm_name = (target_os == femto::TargetOS::Windows) ? "femto_rt_windows.asm" : "femto_rt_linux.asm";
        if (fs::exists(exe_dir / rt_obj_name)) {
            rt_obj = (exe_dir / rt_obj_name).string();
        } else if (fs::exists(exe_dir / "runtime" / rt_obj_name)) {
            rt_obj = (exe_dir / "runtime" / rt_obj_name).string();
        } else if (fs::exists(exe_dir.parent_path() / rt_obj_name)) {
            rt_obj = (exe_dir.parent_path() / rt_obj_name).string();
        } else if (fs::exists(exe_dir.parent_path().parent_path() / rt_obj_name)) {
            rt_obj = (exe_dir.parent_path().parent_path() / rt_obj_name).string();
        } else {
            fs::path rt_asm = fs::path(stdlib_dir).parent_path() / "runtime" / rt_asm_name;
            if (fs::exists(rt_asm)) {
                fs::path target_obj = exe_dir / rt_obj_name;
                std::string assemble_rt_cmd = "nasm -f " + nasm_fmt + " " + quote(rt_asm.string()) + " -o " + quote(target_obj.string());
                if (run_command(assemble_rt_cmd) == 0) {
                    rt_obj = target_obj.string();
                }
            }
        }
    }

    // Link output binary
    if (target_os == femto::TargetOS::Windows) {
#ifdef _WIN32
        MsvcToolchain tc = discover_msvc();
        std::string link_cmd;

        if (tc.is_valid && !tc.link_exe.empty()) {
            link_cmd = quote(tc.link_exe) + " /nologo /subsystem:console /defaultlib:msvcrt /defaultlib:vcruntime /defaultlib:ucrt /defaultlib:oldnames /defaultlib:kernel32 /defaultlib:legacy_stdio_definitions ";
            for (const auto& lp : tc.lib_paths) {
                link_cmd += "/LIBPATH:" + quote(lp) + " ";
            }
            link_cmd += quote(obj_path) + " " + quote(rt_obj) + " /out:" + quote(output_path);
        } else {
            link_cmd = "cl.exe /nologo " + quote(obj_path) + " " + quote(rt_obj) + " /Fe:" + quote(output_path);
        }

        if (run_command(link_cmd) != 0) {
            // Fallback to clang or gcc
            link_cmd = "clang " + quote(obj_path) + " " + quote(rt_obj) + " -o " + quote(output_path);
            if (run_command(link_cmd) != 0) {
                link_cmd = "gcc " + quote(obj_path) + " " + quote(rt_obj) + " -o " + quote(output_path);
                if (run_command(link_cmd) != 0) {
                    std::cerr << "error: linking failed (make sure Visual Studio MSVC tools, Clang, or GCC is installed)\n";
                    if (!keep_temps) { fs::remove(asm_path); fs::remove(obj_path); }
                    return 1;
                }
            }
        }
#else
        // Cross-compiling from Linux to Windows using MinGW
        std::string link_cmd = "x86_64-w64-mingw32-gcc " + quote(obj_path) + " " + quote(rt_obj) + " -o " + quote(output_path);
        if (run_command(link_cmd) != 0) {
            std::cerr << "error: cross-compilation linking failed (make sure x86_64-w64-mingw32-gcc is installed)\n";
            if (!keep_temps) { fs::remove(asm_path); fs::remove(obj_path); }
            return 1;
        }
#endif
    } else {
        std::string link_cmd = "gcc -no-pie " + quote(obj_path) + " " + quote(rt_obj) + " -lm -o " + quote(output_path);
        if (run_command(link_cmd) != 0) {
            std::cerr << "error: linking failed (make sure runtime/femto_rt.o is assembled)\n";
            if (!keep_temps) { fs::remove(asm_path); fs::remove(obj_path); }
            return 1;
        }
    }

    if (!keep_temps) {
        fs::remove(asm_path);
        fs::remove(obj_path);
    }

    std::cout << "Build successful: " << output_path << "\n";
    return 0;
}