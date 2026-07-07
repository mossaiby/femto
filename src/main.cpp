#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/diagnostic.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "parser/ast_dump.h"
#include "sema/type_checker.h"
#include "hir/ast_to_hir.h"
#include "hir/hir_optimizer.h"
#include "lir/hir_to_lir.h"
#include "lir/lir_optimizer.h"
#include "codegen/x86_64/x86_64.h"
#include "codegen/c/c.h"

struct CompilerOptions {
    std::string source_path;
    std::string output_path;
    std::string emit_asm_path;
    std::string emit_obj_path;
    std::string target = "x86_64";
    bool keep_asm = false;
    bool keep_obj = false;
    bool keep_c = false;
    bool emit_hir = false;
    bool emit_lir = false;
    bool show_tokens = false;
    bool show_ast = false;
    bool no_codegen = false;
    bool verbose = false;
    bool show_help = false;
};

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options] <source.femto>\n\n"
        "Options:\n"
        "  -o <file>           Output executable path (default: <source stem>)\n"
        "  --target <x86_64|c> Target backend (default: x86_64)\n"
        "  --emit-asm <file>   Write generated NASM assembly / C source to file\n"
        "  --emit-obj <file>   Write assembled object to file\n"
        "  --keep-asm          Keep intermediate .asm file\n"
        "  --keep-obj          Keep intermediate .o file\n"
        "  --keep-c            Keep intermediate .c file (C backend only)\n"
        "  --emit-hir          Dump HIR to stderr\n"
        "  --emit-lir          Dump LIR to stderr\n"
        "  --tokens            Dump token stream to stderr\n"
        "  --ast               Dump AST to stderr\n"
        "  --no-codegen        Stop after type checking\n"
        "  -v, --verbose       Print pipeline progress\n"
        "  -h, --help          Show this help\n",
        prog);
}

static bool parse_args(int argc, char* argv[], CompilerOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            opts.show_help = true;
            return true;
        } else if (arg == "-o" && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if (arg == "--emit-asm" && i + 1 < argc) {
            opts.emit_asm_path = argv[++i];
        } else if (arg == "--emit-obj" && i + 1 < argc) {
            opts.emit_obj_path = argv[++i];
        } else if (arg == "--target" && i + 1 < argc) {
            opts.target = argv[++i];
            if (opts.target != "x86_64" && opts.target != "c") {
                std::fprintf(stderr, "error: unknown target '%s' (expected x86_64 or c)\n", opts.target.c_str());
                return false;
            }
        } else if (arg == "--keep-asm") {
            opts.keep_asm = true;
        } else if (arg == "--keep-obj") {
            opts.keep_obj = true;
        } else if (arg == "--keep-c") {
            opts.keep_c = true;
        } else if (arg == "--emit-hir") {
            opts.emit_hir = true;
        } else if (arg == "--emit-lir") {
            opts.emit_lir = true;
        } else if (arg == "--tokens") {
            opts.show_tokens = true;
        } else if (arg == "--ast") {
            opts.show_ast = true;
        } else if (arg == "--no-codegen") {
            opts.no_codegen = true;
        } else if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
        } else if (arg[0] != '-' && opts.source_path.empty()) {
            opts.source_path = arg;
        } else {
            std::fprintf(stderr, "error: unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

static void log_verbose(const CompilerOptions& opts, const char* msg) {
    if (opts.verbose) {
        std::fprintf(stderr, "[femto] %s\n", msg);
    }
}

static int run_command(const std::string& cmd, const CompilerOptions& opts) {
    if (opts.verbose) {
        std::fprintf(stderr, "[femto] running: %s\n", cmd.c_str());
    }
    return std::system(cmd.c_str());
}

int main(int argc, char* argv[]) {
    CompilerOptions opts;

    if (!parse_args(argc, argv, opts)) {
        return 1;
    }

    if (opts.show_help || opts.source_path.empty()) {
        print_usage(argv[0]);
        return opts.show_help ? 0 : 1;
    }

    std::filesystem::path source_path = opts.source_path;
    if (!std::filesystem::exists(source_path)) {
        std::fprintf(stderr, "error: file not found: %s\n", source_path.c_str());
        return 1;
    }

    // Default output path: source stem
    if (opts.output_path.empty()) {
        opts.output_path = source_path.stem().string();
    }

    // Default intermediate paths
    std::filesystem::path asm_path = source_path.stem().string() + ".asm";
    std::filesystem::path obj_path = source_path.stem().string() + ".o";
    if (!opts.emit_asm_path.empty()) {
        asm_path = opts.emit_asm_path;
    }
    if (!opts.emit_obj_path.empty()) {
        obj_path = opts.emit_obj_path;
    }

    // Read source
    log_verbose(opts, "Reading source...");
    std::ifstream file(source_path);
    if (!file.is_open()) {
        std::fprintf(stderr, "error: cannot open: %s\n", source_path.c_str());
        return 1;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    // Lex
    log_verbose(opts, "Lexing...");
    femto::DiagnosticEngine diag(source_path.string(), source);
    femto::Lexer lexer(source, diag);
    auto tokens = lexer.tokenize();

    if (opts.show_tokens) {
        for (const auto& tok : tokens) {
            std::fprintf(stderr, "%s\n", tok.to_string().c_str());
        }
    }

    if (diag.has_errors()) {
        diag.print_all();
        return 1;
    }

    // Parse
    log_verbose(opts, "Parsing...");
    femto::Parser parser(tokens, diag);
    auto mod = parser.parse();

    if (opts.show_ast) {
        femto::ast::dump_module(mod);
    }

    if (diag.has_errors()) {
        diag.print_all();
        return 1;
    }

    // Type check
    log_verbose(opts, "Type checking...");
    femto::sema::TypeChecker checker(diag);
    bool ok = false;
    try {
        ok = checker.check(mod);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: type check exception: %s\n", e.what());
        return 1;
    }

    if (diag.has_errors()) {
        diag.print_all();
        return 1;
    }

    if (opts.no_codegen) {
        log_verbose(opts, "Type checking passed (--no-codegen).");
        return 0;
    }

    // AST -> HIR
    log_verbose(opts, "Lowering to HIR...");
    femto::hir::ASTToHIR lowerer(diag);
    auto hir = lowerer.lower(mod);

    if (opts.emit_hir) {
        std::fprintf(stderr, "=== HIR ===\n");
        for (auto& decl : hir.decls) {
            if (auto* func = std::get_if<femto::hir::HIRFunction>(&decl->data)) {
                std::fprintf(stderr, "  func %s\n", func->name.c_str());
            }
        }
    }

    // HIR optimize
    log_verbose(opts, "Optimizing HIR...");
    femto::hir::HIROptimizer hir_opt;
    hir_opt.optimize(hir);

    // HIR -> LIR
    log_verbose(opts, "Lowering to LIR...");
    femto::lir::HIRToLIR hir_to_lir(diag);
    auto lir = hir_to_lir.lower(hir);

    if (opts.emit_lir) {
        std::fprintf(stderr, "=== LIR ===\n");
        for (auto& func : lir.functions) {
            std::fprintf(stderr, "  func %s (ret_type=%s, %zu blocks)\n", func.name.c_str(),
                func.return_type ? femto::sema::type_to_string(*func.return_type).c_str() : "null",
                func.blocks.size());
            for (auto& block : func.blocks) {
                if (!block.label.empty()) {
                    std::fprintf(stderr, "    %s:\n", block.label.c_str());
                }
                for (auto& inst : block.instructions) {
                    std::fprintf(stderr, "      op=%d dest=r%d src1=r%d src2=r%d",
                        static_cast<int>(inst.opcode), inst.dest.id, inst.src1.id, inst.src2.id);
                    if (!inst.label.empty()) std::fprintf(stderr, " label=%s", inst.label.c_str());
                    if (!inst.var_name.empty()) std::fprintf(stderr, " var=%s", inst.var_name.c_str());
                    if (inst.has_imm) std::fprintf(stderr, " imm=%ld", inst.imm);
                    if (inst.has_float_imm) std::fprintf(stderr, " fimm=%f", inst.float_imm);
                    std::fprintf(stderr, "\n");
                }
            }
        }
    }

    // LIR optimize
    log_verbose(opts, "Optimizing LIR...");
    femto::lir::LIROptimizer lir_opt;
    lir_opt.optimize(lir);

    if (opts.target == "c") {
        // ---- C backend ----
        log_verbose(opts, "Generating C source...");
        femto::codegen::c::CCodeGen codegen;
        std::string c_code = codegen.generate(lir);

        // Derive .c path
        std::filesystem::path c_path = source_path.stem().string() + ".c";
        if (!opts.emit_asm_path.empty()) {
            c_path = opts.emit_asm_path;
        }

        // Write C file
        std::ofstream c_file(c_path);
        if (!c_file.is_open()) {
            std::fprintf(stderr, "error: cannot write: %s\n", c_path.c_str());
            return 1;
        }
        c_file << c_code;
        c_file.close();
        log_verbose(opts, ("Wrote C source: " + c_path.string()).c_str());

        // Compile with cc
        log_verbose(opts, "Compiling with cc...");
        {
            std::string cc_cmd = "cc -o " + opts.output_path + " " + c_path.string();
            int result = run_command(cc_cmd, opts);
            if (result != 0) {
                std::fprintf(stderr, "error: cc failed with exit code %d\n", result);
                if (!opts.keep_c) std::filesystem::remove(c_path);
                return 1;
            }
        }

        // Cleanup
        if (!opts.keep_c) std::filesystem::remove(c_path);

        log_verbose(opts, ("Done: " + opts.output_path).c_str());
        return 0;
    }

    // ---- x86-64 / NASM backend ----
    log_verbose(opts, "Generating x86-64 assembly...");
    femto::codegen::x86_64::X86_64CodeGen codegen;
    std::string asm_code = codegen.generate(lir);

    // Write assembly file
    std::ofstream asm_file(asm_path);
    if (!asm_file.is_open()) {
        std::fprintf(stderr, "error: cannot write: %s\n", asm_path.c_str());
        return 1;
    }
    asm_file << asm_code;
    asm_file.close();
    log_verbose(opts, ("Wrote assembly: " + asm_path.string()).c_str());

    if (opts.emit_asm_path.empty() && !opts.keep_asm) {
        // Will clean up later
    }

    // Assemble with nasm
    log_verbose(opts, "Assembling with nasm...");
    {
        std::string nasm_cmd = "nasm -f elf64 " + asm_path.string() + " -o " + obj_path.string();
        int result = run_command(nasm_cmd, opts);
        if (result != 0) {
            std::fprintf(stderr, "error: nasm failed with exit code %d\n", result);
            if (!opts.keep_asm) std::filesystem::remove(asm_path);
            return 1;
        }
    }

    // Link with ld
    log_verbose(opts, "Linking...");
    {
        std::string ld_cmd = "ld -o " + opts.output_path + " " + obj_path.string();
        int result = run_command(ld_cmd, opts);
        if (result != 0) {
            std::fprintf(stderr, "error: linker failed with exit code %d\n", result);
            if (!opts.keep_asm) std::filesystem::remove(asm_path);
            if (!opts.keep_obj) std::filesystem::remove(obj_path);
            return 1;
        }
    }

    // Cleanup intermediates
    if (!opts.keep_asm) std::filesystem::remove(asm_path);
    if (!opts.keep_obj) std::filesystem::remove(obj_path);

    log_verbose(opts, ("Done: " + opts.output_path).c_str());
    return 0;
}
