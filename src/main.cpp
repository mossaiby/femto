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

struct CompilerOptions {
    std::string source_path;
    std::string output_path;
    std::string emit_asm_path;
    std::string emit_obj_path;
    bool keep_asm = false;
    bool keep_obj = false;
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
        "  --emit-asm <file>   Write generated NASM assembly to file\n"
        "  --emit-obj <file>   Write assembled object to file\n"
        "  --keep-asm          Keep intermediate .asm file\n"
        "  --keep-obj          Keep intermediate .o file\n"
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
        } else if (arg == "--keep-asm") {
            opts.keep_asm = true;
        } else if (arg == "--keep-obj") {
            opts.keep_obj = true;
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
            std::fprintf(stderr, "  func %s (%zu blocks)\n", func.name.c_str(), func.blocks.size());
            for (auto& block : func.blocks) {
                if (!block.label.empty()) {
                    std::fprintf(stderr, "    %s:\n", block.label.c_str());
                }
                for (auto& inst : block.instructions) {
                    std::fprintf(stderr, "      op=%d dest=r%d src1=r%d src2=r%d\n",
                        static_cast<int>(inst.opcode), inst.dest.id, inst.src1.id, inst.src2.id);
                }
            }
        }
    }

    // LIR optimize
    log_verbose(opts, "Optimizing LIR...");
    femto::lir::LIROptimizer lir_opt;
    lir_opt.optimize(lir);

    // LIR -> NASM assembly
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
