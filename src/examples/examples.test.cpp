#include <gtest/gtest.h>
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

namespace {

struct ExampleResult {
    std::string filename;
    bool parsed;
    bool type_checked;
    bool lowered_to_hir;
    bool optimized;
    std::string error;
};

ExampleResult compile_example(const std::string& path) {
    ExampleResult result;
    result.filename = std::filesystem::path(path).filename().string();
    result.parsed = false;
    result.type_checked = false;
    result.lowered_to_hir = false;
    result.optimized = false;

    // Read source
    std::ifstream file(path);
    if (!file.is_open()) {
        result.error = "cannot open file";
        return result;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    // Lex
    femto::DiagnosticEngine diag(result.filename, source);
    femto::Lexer lexer(source, diag);
    auto tokens = lexer.tokenize();

    if (diag.has_errors()) {
        result.error = "lexer errors";
        return result;
    }

    // Parse
    femto::Parser parser(tokens, diag);
    auto mod = parser.parse();

    if (diag.has_errors()) {
        result.error = "parser errors";
        return result;
    }
    result.parsed = true;

    // Type check
    femto::sema::TypeChecker checker(diag);
    bool ok = false;
    try {
        ok = checker.check(mod);
    } catch (const std::exception& e) {
        result.error = std::string("type_check exception: ") + e.what();
        diag.print_all();
        return result;
    }

    if (diag.has_errors()) {
        std::ofstream errlog("/tmp/structs_errors.log");
        for (auto& d : diag.diagnostics()) {
            errlog << result.filename << ":" << d.location.line << ":"
                   << d.location.column << ": error: " << d.message << std::endl;
        }
        errlog.close();
        result.error = "type errors";
        return result;
    }
    result.type_checked = true;

    // Lower to HIR
    femto::hir::ASTToHIR lowerer(diag);
    try {
        auto hir = lowerer.lower(mod);
        result.lowered_to_hir = true;

        // Optimize HIR
        femto::hir::HIROptimizer optimizer;
        optimizer.optimize(hir);
        result.optimized = true;
    } catch (const std::exception& e) {
        result.error = std::string("hir exception: ") + e.what();
        diag.print_all();
        return result;
    }

    return result;
}

} // namespace

class ExamplesTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ExamplesTest, CompilesSuccessfully) {
    auto result = compile_example(GetParam());
    EXPECT_TRUE(result.parsed) << result.filename << ": parse failed - " << result.error;
    EXPECT_TRUE(result.type_checked) << result.filename << ": type check failed - " << result.error;
    EXPECT_TRUE(result.lowered_to_hir) << result.filename << ": HIR lowering failed - " << result.error;
    EXPECT_TRUE(result.optimized) << result.filename << ": optimization failed - " << result.error;
}

// List all example files
std::vector<std::string> get_example_files() {
    std::vector<std::string> examples;
    std::string examples_dir = EXAMPLES_DIR;
    for (auto& entry : std::filesystem::directory_iterator(examples_dir)) {
        if (entry.path().extension() == ".femto") {
            examples.push_back(entry.path().string());
        }
    }
    std::sort(examples.begin(), examples.end());
    return examples;
}

INSTANTIATE_TEST_SUITE_P(
    AllExamples,
    ExamplesTest,
    ::testing::ValuesIn(get_example_files()),
    [](const ::testing::TestParamInfo<std::string>& info) {
        auto name = std::filesystem::path(info.param).stem().string();
        // Replace non-alphanumeric characters with underscores
        for (auto& c : name) {
            if (!std::isalnum(c)) c = '_';
        }
        return name;
    }
);
