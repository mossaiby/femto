#include "test_framework.hpp"
#include "lsp/json.hpp"
#include "lsp/lsp_server.hpp"

using namespace femto::lsp;

TEST_CASE(Lsp, JsonParserAndSerializer) {
    std::string json_str = "{\"method\":\"initialize\",\"params\":{\"processId\":1234,\"rootUri\":null,\"capabilities\":{}},\"arr\":[1,2,true,\"test\"]}";
    JsonValue v = JsonValue::parse(json_str);

    ASSERT_TRUE(v.is_object());
    ASSERT_STREQ(v["method"].as_string(), "initialize");
    ASSERT_EQ(v["params"]["processId"].as_int(), 1234);
    ASSERT_TRUE(v["params"]["rootUri"].is_null());

    const auto& arr = v["arr"].as_array();
    ASSERT_EQ(arr.size(), 4u);
    ASSERT_EQ(arr[0].as_int(), 1);
    ASSERT_EQ(arr[1].as_int(), 2);
    ASSERT_TRUE(arr[2].as_bool());
    ASSERT_STREQ(arr[3].as_string(), "test");
}

TEST_CASE(Lsp, DiagnosticReportingOnErrors) {
    LspServer server;
    std::string invalid_code =
        "#export\n"
        "main :: () -> int32 {\n"
        "    int32 x = \"type mismatch string\";\n"
        "    return x;\n"
        "}\n";

    JsonObject res = server.analyze_document("file:///test.femto", invalid_code);
    const auto& diags = res["diagnostics"].as_array();

    ASSERT_TRUE(diags.size() >= 1u);
    ASSERT_EQ(diags[0]["severity"].as_int(), 1);
    ASSERT_STREQ(diags[0]["source"].as_string(), "femtoc");
}

TEST_CASE(Lsp, DocumentSymbolExtractionAndNavigation) {
    LspServer server;
    std::string source =
        "Point :: struct {\n"
        "    int32 x = 0;\n"
        "    int32 y = 0;\n"
        "}\n"
        "#export\n"
        "compute :: (int32 a, int32 b = 10) -> int32 {\n"
        "    return a * 2 + b;\n"
        "}\n"
        "main :: () -> int32 {\n"
        "    return compute(5, 6);\n"
        "}\n";

    server.open_document("file:///symbols.femto", source);
    server.analyze_document("file:///symbols.femto", source);

    JsonArray syms = server.handle_document_symbols("file:///symbols.femto");

    ASSERT_TRUE(syms.size() >= 2u);
    bool found_fn = false, found_st = false;
    for (const auto& s : syms) {
        if (s["name"].as_string() == "compute" && s["kind"].as_int() == 12) found_fn = true;
        if (s["name"].as_string() == "Point" && s["kind"].as_int() == 23) found_st = true;
    }
    ASSERT_TRUE(found_fn);
    ASSERT_TRUE(found_st);

    // Test hover on 'compute' call site inside main (line 10 in 1-based -> line 9, col 13 in 0-based)
    JsonObject hover = server.handle_hover("file:///symbols.femto", 9, 13);
    ASSERT_FALSE(hover.empty());
    ASSERT_TRUE(hover["contents"]["value"].as_string().find("compute") != std::string::npos);

    // Test completion items
    JsonArray completions = server.handle_completion("file:///symbols.femto", 6, 0);
    ASSERT_TRUE(completions.size() > 5u);

    // Test F12 definition jump from call site 'compute(5, 6)' on line 10 to line 6 declaration
    JsonValue def = server.handle_definition("file:///symbols.femto", 9, 13);
    ASSERT_FALSE(def.is_null());
    ASSERT_EQ(def["range"]["start"]["line"].as_int(), 5);

    // Test signature help for compute(5, 6) inside main
    JsonObject sig_help = server.handle_signature_help("file:///symbols.femto", 9, 23);
    ASSERT_FALSE(sig_help.empty());
    ASSERT_EQ(sig_help["activeParameter"].as_int(), 1);
}

TEST_CASE(Lsp, ReferencesAndRenameAndFormatting) {
    LspServer server;
    std::string source =
        "#export\n"
        "multiply :: (int32 val) -> int32 {\n"
        "    return val * 2;\n"
        "}\n"
        "main :: () -> int32 {\n"
        "    int32 x = multiply(10);\n"
        "    return multiply(x);\n"
        "}\n";

    server.open_document("file:///ref_test.femto", source);
    server.analyze_document("file:///ref_test.femto", source);

    // 1. References on 'multiply'
    JsonArray refs = server.handle_references("file:///ref_test.femto", 1, 4, true);
    ASSERT_TRUE(refs.size() >= 3u); // Declaration + 2 call sites

    // 2. Document Highlights on 'multiply'
    JsonArray highlights = server.handle_document_highlights("file:///ref_test.femto", 1, 4);
    ASSERT_TRUE(highlights.size() >= 3u);

    // 3. Prepare Rename
    JsonValue prep = server.handle_prepare_rename("file:///ref_test.femto", 1, 4);
    ASSERT_FALSE(prep.is_null());
    ASSERT_STREQ(prep["placeholder"].as_string(), "multiply");

    // 4. Rename 'multiply' -> 'double_val'
    JsonObject rename_res = server.handle_rename("file:///ref_test.femto", 1, 4, "double_val");
    ASSERT_TRUE(rename_res.contains("changes"));
    const auto& edits = rename_res["changes"]["file:///ref_test.femto"].as_array();
    ASSERT_TRUE(edits.size() >= 3u);
    ASSERT_STREQ(edits[0]["newText"].as_string(), "double_val");

    // 5. Document Formatting
    JsonArray format_edits = server.handle_formatting("file:///ref_test.femto", 4, true);
    ASSERT_EQ(format_edits.size(), 1u);
    ASSERT_TRUE(format_edits[0]["newText"].as_string().find("    return val * 2;") != std::string::npos);
}