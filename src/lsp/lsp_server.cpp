#include "lsp/lsp_server.hpp"
#include "common/module_loader.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/type_checker.hpp"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <functional>
#include <algorithm>

#ifndef FEMTO_STDLIB_DIR
#define FEMTO_STDLIB_DIR "stdlib"
#endif

namespace femto::lsp {

namespace fs = std::filesystem;

static uint32_t get_byte_offset(const SourceManager& sm, uint32_t line_0, uint32_t char_0) {
    std::string_view src = sm.source();
    uint32_t cur_line = 0;
    uint32_t cur_col = 0;
    for (uint32_t i = 0; i < src.size(); ++i) {
        if (cur_line == line_0 && cur_col == char_0) return i;
        if (src[i] == '\n') {
            if (cur_line == line_0) return i;
            cur_line++;
            cur_col = 0;
        } else {
            cur_col++;
        }
    }
    return (uint32_t)src.size();
}

static std::string get_word_at_pos(const std::string& source, uint32_t offset) {
    if (source.empty() || offset > source.size()) return "";
    if (offset == source.size() && offset > 0) offset--;

    if (!std::isalnum((unsigned char)source[offset]) && source[offset] != '_' && source[offset] != ':') {
        if (offset > 0 && (std::isalnum((unsigned char)source[offset - 1]) || source[offset - 1] == '_')) {
            offset--;
        } else {
            return "";
        }
    }

    int start = (int)offset;
    while (start > 0 && (std::isalnum((unsigned char)source[start - 1]) || source[start - 1] == '_' || source[start - 1] == ':')) {
        start--;
    }

    int end = (int)offset;
    while (end < (int)source.size() && (std::isalnum((unsigned char)source[end]) || source[end] == '_' || source[end] == ':')) {
        end++;
    }

    return source.substr(start, end - start);
}

static std::string path_to_uri(const std::string& path) {
    if (path.rfind("file://", 0) == 0) return path;
    if (path.empty()) return "file:///";
    if (path.front() == '/') return "file://" + path;
    return "file:///" + path;
}

LspServer::LspServer() {
    search_paths_.push_back(".");
    if (fs::exists(FEMTO_STDLIB_DIR)) {
        search_paths_.push_back(FEMTO_STDLIB_DIR);
    }
    try {
        fs::path exe_dir = fs::canonical("/proc/self/exe").parent_path();
        if (fs::exists(exe_dir / "stdlib")) {
            search_paths_.push_back((exe_dir / "stdlib").string());
        }
        if (fs::exists(exe_dir.parent_path() / "stdlib")) {
            search_paths_.push_back((exe_dir.parent_path() / "stdlib").string());
        }
    } catch (...) {}
}

std::string LspServer::uri_to_path(const std::string& uri) {
    if (uri.rfind("file://", 0) == 0) {
        return uri.substr(7);
    }
    return uri;
}

void LspServer::send_response(const JsonValue& id, const JsonValue& result) {
    JsonObject resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["result"] = result;

    std::string body = JsonValue(resp).serialize();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

void LspServer::send_notification(const std::string& method, const JsonValue& params) {
    JsonObject notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = method;
    notif["params"] = params;

    std::string body = JsonValue(notif).serialize();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

void LspServer::send_diagnostics(const std::string& uri, const JsonArray& diagnostics) {
    JsonObject params;
    params["uri"] = uri;
    params["diagnostics"] = diagnostics;
    send_notification("textDocument/publishDiagnostics", params);
}

JsonObject LspServer::analyze_document(const std::string& uri, const std::string& source) {
    documents_[uri] = source;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, /*print_to_stderr=*/false);
    Arena arena;

    std::vector<std::string> current_search_paths = search_paths_;
    fs::path doc_dir = fs::path(filepath).parent_path();
    if (fs::exists(doc_dir)) {
        current_search_paths.insert(current_search_paths.begin(), doc_dir.string());
    }

    ModuleLoader loader(current_search_paths, arena);
    ASTProgram prog;
    loader.load_module_from_source(filepath, source, "", prog, diag);

    if (!diag.has_errors()) {
        TypeChecker checker(arena, diag);
        checker.check_program(prog);
    }

    JsonArray diag_arr;
    for (const auto& item : diag.items()) {
        auto lc = sm.get_line_col(item.span.start);
        uint32_t line_0 = lc.line > 0 ? lc.line - 1 : 0;
        uint32_t char_0 = lc.col > 0 ? lc.col - 1 : 0;
        uint32_t char_end = char_0 + std::max(1u, item.span.length);

        JsonObject range;
        JsonObject start_pos;
        start_pos["line"] = line_0;
        start_pos["character"] = char_0;
        JsonObject end_pos;
        end_pos["line"] = line_0;
        end_pos["character"] = char_end;
        range["start"] = start_pos;
        range["end"] = end_pos;

        JsonObject d;
        d["range"] = range;
        d["severity"] = 1; // Error
        d["source"] = "femtoc";
        d["message"] = item.message;

        diag_arr.push_back(d);
    }

    JsonObject res;
    res["diagnostics"] = diag_arr;
    return res;
}

JsonObject LspServer::handle_hover(const std::string& uri, uint32_t line_0, uint32_t char_0) {
    auto it = documents_.find(uri);
    if (it == documents_.end()) return JsonObject{};

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, false);
    Arena arena;

    std::vector<std::string> current_search_paths = search_paths_;
    fs::path doc_dir = fs::path(filepath).parent_path();
    if (fs::exists(doc_dir)) {
        current_search_paths.insert(current_search_paths.begin(), doc_dir.string());
    }

    ModuleLoader loader(current_search_paths, arena);
    ASTProgram prog;
    loader.load_module_from_source(filepath, source, "", prog, diag);
    TypeChecker checker(arena, diag);
    checker.check_program(prog);

    uint32_t offset = get_byte_offset(sm, line_0, char_0);
    std::string symbol_name = get_word_at_pos(source, offset);
    if (symbol_name.empty()) return JsonObject{};

    std::string hover_text;

    // 1. Check local variables & parameters
    for (const auto* fn : prog.functions) {
        for (const auto& p : fn->params) {
            if (p.name == symbol_name) {
                hover_text = "(parameter) " + TypeChecker::get_type_name(p.type) + " " + std::string(p.name);
                break;
            }
        }
        if (!hover_text.empty()) break;

        std::function<void(const ASTStmt*)> check_stmt;
        check_stmt = [&](const ASTStmt* s) {
            if (!s || !hover_text.empty()) return;
            if (s->kind == StmtKind::VarDecl && s->name == symbol_name) {
                std::string prefix = s->is_const ? "const " : "";
                hover_text = "(local variable) " + prefix + TypeChecker::get_type_name(s->type_annot) + " " + std::string(s->name);
                return;
            }
            for (const auto* ts : s->then_block) check_stmt(ts);
            for (const auto* es : s->else_block) check_stmt(es);
            for (const auto* ss : s->success_block) check_stmt(ss);
            for (const auto* fs : s->failure_block) check_stmt(fs);
            for (const auto& sc : s->switch_cases) {
                for (const auto* bs : sc.body) check_stmt(bs);
            }
        };

        for (const auto* s : fn->body) {
            check_stmt(s);
            if (!hover_text.empty()) break;
        }
        if (!hover_text.empty()) break;
    }

    // 2. Check functions
    if (hover_text.empty()) {
        for (const auto* fn : prog.functions) {
            if (fn->name == symbol_name || fn->name.ends_with("::" + symbol_name)) {
                std::ostringstream oss;
                oss << "(function) " << fn->name << " :: (";
                for (size_t i = 0; i < fn->params.size(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << TypeChecker::get_type_name(fn->params[i].type) << " " << fn->params[i].name;
                }
                if (fn->is_variadic) oss << ", ...";
                oss << ") -> " << TypeChecker::get_type_name(fn->return_type);
                hover_text = oss.str();
                break;
            }
        }
    }

    // 3. Check structs & fields
    if (hover_text.empty()) {
        for (const auto* st : prog.structs) {
            if (st->name == symbol_name || st->name.ends_with("::" + symbol_name)) {
                hover_text = "struct " + std::string(st->name);
                break;
            }
            for (const auto& f : st->fields) {
                if (f.name == symbol_name) {
                    hover_text = "(field) " + TypeChecker::get_type_name(f.type) + " " + std::string(f.name);
                    break;
                }
            }
            if (!hover_text.empty()) break;
        }
    }

    // 4. Check constants
    if (hover_text.empty()) {
        for (const auto* c : prog.constants) {
            if (c->name == symbol_name || c->name.ends_with("::" + symbol_name)) {
                hover_text = "constant " + std::string(c->name);
                break;
            }
        }
    }

    if (hover_text.empty()) return JsonObject{};

    JsonObject contents;
    contents["kind"] = "markdown";
    contents["value"] = "```femto\n" + hover_text + "\n```";

    JsonObject result;
    result["contents"] = contents;
    return result;
}

JsonValue LspServer::handle_definition(const std::string& uri, uint32_t line_0, uint32_t char_0) {
    auto it = documents_.find(uri);
    if (it == documents_.end()) return nullptr;

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, false);
    Arena arena;

    std::vector<std::string> current_search_paths = search_paths_;
    fs::path doc_dir = fs::path(filepath).parent_path();
    if (fs::exists(doc_dir)) {
        current_search_paths.insert(current_search_paths.begin(), doc_dir.string());
    }

    ModuleLoader loader(current_search_paths, arena);
    ASTProgram prog;
    loader.load_module_from_source(filepath, source, "", prog, diag);

    uint32_t offset = get_byte_offset(sm, line_0, char_0);
    std::string symbol_name = get_word_at_pos(source, offset);
    if (symbol_name.empty()) return nullptr;

    auto make_location = [&](const std::string& target_file, SourceSpan span) -> JsonObject {
        std::string target_source = source;
        if (target_file != filepath && fs::exists(target_file)) {
            std::ifstream f(target_file);
            target_source = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }
        SourceManager target_sm(target_file, target_source);
        auto lc = target_sm.get_line_col(span.start);

        JsonObject range;
        JsonObject pos_start;
        pos_start["line"] = lc.line > 0 ? lc.line - 1 : 0;
        pos_start["character"] = lc.col > 0 ? lc.col - 1 : 0;

        JsonObject pos_end;
        pos_end["line"] = lc.line > 0 ? lc.line - 1 : 0;
        pos_end["character"] = pos_start["character"].as_int() + std::max(1u, span.length);

        range["start"] = pos_start;
        range["end"] = pos_end;

        JsonObject loc;
        loc["uri"] = path_to_uri(target_file.empty() ? filepath : target_file);
        loc["range"] = range;
        return loc;
    };

    // 1. Check local variables & parameters
    for (const auto* fn : prog.functions) {
        for (const auto& p : fn->params) {
            if (p.name == symbol_name) {
                return make_location(fn->file_path.empty() ? filepath : fn->file_path, p.span);
            }
        }

        const ASTStmt* found_var = nullptr;
        std::function<void(const ASTStmt*)> check_stmt;
        check_stmt = [&](const ASTStmt* s) {
            if (!s || found_var) return;
            if (s->kind == StmtKind::VarDecl && s->name == symbol_name) {
                found_var = s;
                return;
            }
            for (const auto* ts : s->then_block) check_stmt(ts);
            for (const auto* es : s->else_block) check_stmt(es);
            for (const auto* ss : s->success_block) check_stmt(ss);
            for (const auto* fs : s->failure_block) check_stmt(fs);
            for (const auto& sc : s->switch_cases) {
                for (const auto* bs : sc.body) check_stmt(bs);
            }
        };

        for (const auto* s : fn->body) {
            check_stmt(s);
            if (found_var) {
                return make_location(fn->file_path.empty() ? filepath : fn->file_path, found_var->span);
            }
        }
    }

    // 2. Check functions
    for (const auto* fn : prog.functions) {
        if (fn->name == symbol_name || fn->name.ends_with("::" + symbol_name)) {
            return make_location(fn->file_path.empty() ? filepath : fn->file_path, fn->span);
        }
    }

    // 3. Check structs & fields
    for (const auto* st : prog.structs) {
        if (st->name == symbol_name || st->name.ends_with("::" + symbol_name)) {
            return make_location(st->file_path.empty() ? filepath : st->file_path, st->span);
        }
        for (const auto& f : st->fields) {
            if (f.name == symbol_name) {
                return make_location(st->file_path.empty() ? filepath : st->file_path, f.span);
            }
        }
    }

    // 4. Check constants
    for (const auto* c : prog.constants) {
        if (c->name == symbol_name || c->name.ends_with("::" + symbol_name)) {
            return make_location(c->file_path.empty() ? filepath : c->file_path, c->span);
        }
    }

    return nullptr;
}

JsonArray LspServer::handle_references(const std::string& uri, uint32_t line_0, uint32_t char_0, bool include_decl) {
    JsonArray refs;
    auto it = documents_.find(uri);
    if (it == documents_.end()) return refs;

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, false);

    uint32_t offset = get_byte_offset(sm, line_0, char_0);
    std::string symbol_name = get_word_at_pos(source, offset);
    if (symbol_name.empty()) return refs;

    Lexer lexer(sm, diag);
    while (true) {
        Token tok = lexer.next_token();
        if (tok.kind == TokenKind::Eof) break;
        if (tok.kind == TokenKind::Identifier && tok.text == symbol_name) {
            auto lc = sm.get_line_col(tok.span.start);
            JsonObject range;
            JsonObject pos_start;
            pos_start["line"] = lc.line > 0 ? lc.line - 1 : 0;
            pos_start["character"] = lc.col > 0 ? lc.col - 1 : 0;

            JsonObject pos_end;
            pos_end["line"] = lc.line > 0 ? lc.line - 1 : 0;
            pos_end["character"] = pos_start["character"].as_int() + (int)tok.text.size();

            range["start"] = pos_start;
            range["end"] = pos_end;

            JsonObject loc;
            loc["uri"] = uri;
            loc["range"] = range;
            refs.push_back(loc);
        }
    }

    return refs;
}

JsonArray LspServer::handle_document_highlights(const std::string& uri, uint32_t line_0, uint32_t char_0) {
    JsonArray highlights;
    auto it = documents_.find(uri);
    if (it == documents_.end()) return highlights;

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, false);

    uint32_t offset = get_byte_offset(sm, line_0, char_0);
    std::string symbol_name = get_word_at_pos(source, offset);
    if (symbol_name.empty()) return highlights;

    Lexer lexer(sm, diag);
    while (true) {
        Token tok = lexer.next_token();
        if (tok.kind == TokenKind::Eof) break;
        if (tok.kind == TokenKind::Identifier && tok.text == symbol_name) {
            auto lc = sm.get_line_col(tok.span.start);
            JsonObject range;
            JsonObject pos_start;
            pos_start["line"] = lc.line > 0 ? lc.line - 1 : 0;
            pos_start["character"] = lc.col > 0 ? lc.col - 1 : 0;

            JsonObject pos_end;
            pos_end["line"] = lc.line > 0 ? lc.line - 1 : 0;
            pos_end["character"] = pos_start["character"].as_int() + (int)tok.text.size();

            range["start"] = pos_start;
            range["end"] = pos_end;

            JsonObject hl;
            hl["range"] = range;
            hl["kind"] = 1; // Text
            highlights.push_back(hl);
        }
    }

    return highlights;
}

JsonValue LspServer::handle_prepare_rename(const std::string& uri, uint32_t line_0, uint32_t char_0) {
    auto it = documents_.find(uri);
    if (it == documents_.end()) return nullptr;

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);

    uint32_t offset = get_byte_offset(sm, line_0, char_0);
    std::string symbol_name = get_word_at_pos(source, offset);
    if (symbol_name.empty()) return nullptr;

    auto lc = sm.get_line_col(SourceLocation{offset});
    JsonObject range;
    JsonObject pos_start;
    pos_start["line"] = line_0;
    pos_start["character"] = lc.col > 0 ? lc.col - 1 : 0;

    JsonObject pos_end;
    pos_end["line"] = line_0;
    pos_end["character"] = pos_start["character"].as_int() + (int)symbol_name.size();

    range["start"] = pos_start;
    range["end"] = pos_end;

    JsonObject res;
    res["range"] = range;
    res["placeholder"] = symbol_name;
    return res;
}

JsonObject LspServer::handle_rename(const std::string& uri, uint32_t line_0, uint32_t char_0, const std::string& new_name) {
    JsonObject result;
    auto it = documents_.find(uri);
    if (it == documents_.end()) return result;

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, false);

    uint32_t offset = get_byte_offset(sm, line_0, char_0);
    std::string symbol_name = get_word_at_pos(source, offset);
    if (symbol_name.empty()) return result;

    JsonArray edits;
    Lexer lexer(sm, diag);
    while (true) {
        Token tok = lexer.next_token();
        if (tok.kind == TokenKind::Eof) break;
        if (tok.kind == TokenKind::Identifier && tok.text == symbol_name) {
            auto lc = sm.get_line_col(tok.span.start);
            JsonObject range;
            JsonObject pos_start;
            pos_start["line"] = lc.line > 0 ? lc.line - 1 : 0;
            pos_start["character"] = lc.col > 0 ? lc.col - 1 : 0;

            JsonObject pos_end;
            pos_end["line"] = lc.line > 0 ? lc.line - 1 : 0;
            pos_end["character"] = pos_start["character"].as_int() + (int)tok.text.size();

            range["start"] = pos_start;
            range["end"] = pos_end;

            JsonObject edit;
            edit["range"] = range;
            edit["newText"] = new_name;
            edits.push_back(edit);
        }
    }

    JsonObject changes;
    changes[uri] = edits;
    result["changes"] = changes;
    return result;
}

JsonArray LspServer::handle_formatting(const std::string& uri, uint32_t tab_size, bool insert_spaces) {
    JsonArray edits;
    auto it = documents_.find(uri);
    if (it == documents_.end()) return edits;

    const std::string& source = it->second;
    std::istringstream stream(source);
    std::string line;
    std::ostringstream formatted;
    int indent_level = 0;
    std::string indent_unit = insert_spaces ? std::string(tab_size > 0 ? tab_size : 4, ' ') : "\t";
    int line_count = 0;
    int empty_lines = 0;

    while (std::getline(stream, line)) {
        line_count++;
        // Trim leading and trailing whitespace
        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            empty_lines++;
            if (empty_lines <= 1) {
                formatted << "\n";
            }
            continue;
        }
        empty_lines = 0;
        size_t last = line.find_last_not_of(" \t\r");
        std::string trimmed = line.substr(first, last - first + 1);

        // Adjust indent for closing braces
        if (trimmed.front() == '}' || trimmed.rfind("} else", 0) == 0 || trimmed.rfind("} #else", 0) == 0) {
            indent_level = std::max(0, indent_level - 1);
        }

        for (int i = 0; i < indent_level; ++i) {
            formatted << indent_unit;
        }
        formatted << trimmed << "\n";

        // Adjust indent for opening braces
        int open_count = 0;
        int close_count = 0;
        bool in_str = false;
        for (size_t i = 0; i < trimmed.size(); ++i) {
            if (trimmed[i] == '"' && (i == 0 || trimmed[i - 1] != '\\')) in_str = !in_str;
            if (!in_str) {
                if (trimmed[i] == '{') open_count++;
                if (trimmed[i] == '}') close_count++;
            }
        }
        if (open_count > close_count && trimmed.front() != '}') {
            indent_level += (open_count - close_count);
        } else if (trimmed.front() == '}' && open_count > close_count) {
            indent_level += (open_count - close_count);
        }
    }

    JsonObject range;
    JsonObject pos_start;
    pos_start["line"] = 0;
    pos_start["character"] = 0;

    JsonObject pos_end;
    pos_end["line"] = line_count + 1;
    pos_end["character"] = 0;

    range["start"] = pos_start;
    range["end"] = pos_end;

    JsonObject edit;
    edit["range"] = range;
    edit["newText"] = formatted.str();
    edits.push_back(edit);

    return edits;
}

JsonArray LspServer::handle_completion(const std::string& uri, uint32_t line_0, uint32_t char_0) {
    JsonArray items;

    auto add_item = [&](std::string label, int kind, std::string detail = "", std::string insert_text = "") {
        JsonObject item;
        item["label"] = label;
        item["kind"] = kind; // 14: Keyword, 7: Struct, 3: Function, 5: Field, 15: Snippet
        if (!detail.empty()) item["detail"] = detail;
        if (!insert_text.empty()) {
            item["insertText"] = insert_text;
            item["insertTextFormat"] = 2; // Snippet format
        }
        items.push_back(item);
    };

    // Standard Keywords & Snippets
    static const std::vector<std::pair<std::string, std::string>> snippets = {
        {"if", "if (${1:condition}) then {\n\t$0\n}"},
        {"while", "while (${1:condition}) {\n\t$0\n}"},
        {"for", "for (${1:int32 i = 0}; ${2:i < 10}; ${3:i++}) {\n\t$0\n}"},
        {"match", "match (${1:expr}) {\n\t# == ${2:val} { $0 }\n\tdefault {  }\n};"},
        {"defer", "defer ${1:cleanup_stmt};"},
        {"struct", "${1:Name} :: struct {\n\t$0\n}"},
        {"fn", "${1:name} :: (${2:params}) -> ${3:void} {\n\t$0\n}"}
    };
    for (const auto& sn : snippets) add_item(sn.first, 15, "snippet", sn.second);

    static const std::vector<std::string> keywords = {
        "then", "else", "do", "switch", "case", "default", "foreach", "in",
        "break", "continue", "return", "import", "namespace", "extern", "const",
        "any", "success", "failure"
    };
    for (const auto& kw : keywords) add_item(kw, 14, "keyword");

    // Standard Primitives
    static const std::vector<std::string> primitives = {
        "int8", "int16", "int32", "int64", "int128", "uint8", "uint16", "uint32",
        "uint64", "uint128", "float32", "float64", "bool8", "char8", "char16", "char32",
        "string8", "string16", "string32", "void", "any"
    };
    for (const auto& pr : primitives) add_item(pr, 7, "primitive type");

    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        const std::string& source = it->second;
        std::string filepath = uri_to_path(uri);
        SourceManager sm(filepath, source);
        Diagnostics diag(sm, false);
        Arena arena;

        std::vector<std::string> current_search_paths = search_paths_;
        fs::path doc_dir = fs::path(filepath).parent_path();
        if (fs::exists(doc_dir)) {
            current_search_paths.insert(current_search_paths.begin(), doc_dir.string());
        }

        ModuleLoader loader(current_search_paths, arena);
        ASTProgram prog;
        loader.load_module_from_source(filepath, source, "", prog, diag);

        // Check if completing after '.' (member completion)
        uint32_t offset = get_byte_offset(sm, line_0, char_0);
        if (offset > 0 && offset <= source.size() && source[offset - 1] == '.') {
            for (const auto* st : prog.structs) {
                for (const auto& f : st->fields) {
                    add_item(std::string(f.name), 5, "(field) " + TypeChecker::get_type_name(f.type));
                }
            }
            return items;
        }

        for (const auto* fn : prog.functions) {
            add_item(std::string(fn->name), 3, "(function) " + std::string(fn->name));
        }
        for (const auto* st : prog.structs) {
            add_item(std::string(st->name), 7, "struct " + std::string(st->name));
        }
        for (const auto* c : prog.constants) {
            add_item(std::string(c->name), 21, "constant " + std::string(c->name));
        }
    }

    return items;
}

JsonObject LspServer::handle_signature_help(const std::string& uri, uint32_t line_0, uint32_t char_0) {
    auto it = documents_.find(uri);
    if (it == documents_.end()) return JsonObject{};

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, false);
    Arena arena;

    std::vector<std::string> current_search_paths = search_paths_;
    fs::path doc_dir = fs::path(filepath).parent_path();
    if (fs::exists(doc_dir)) {
        current_search_paths.insert(current_search_paths.begin(), doc_dir.string());
    }

    ModuleLoader loader(current_search_paths, arena);
    ASTProgram prog;
    loader.load_module_from_source(filepath, source, "", prog, diag);

    uint32_t offset = get_byte_offset(sm, line_0, char_0);
    if (offset == 0 || offset > source.size()) return JsonObject{};

    int comma_count = 0;
    int paren_depth = 0;
    std::string callee_name;

    for (int i = (int)offset - 1; i >= 0; --i) {
        char c = source[i];
        if (c == ')') paren_depth++;
        else if (c == '(') {
            if (paren_depth > 0) paren_depth--;
            else {
                int end_id = i - 1;
                while (end_id >= 0 && std::isspace((unsigned char)source[end_id])) end_id--;
                int start_id = end_id;
                while (start_id >= 0 && (std::isalnum((unsigned char)source[start_id]) || source[start_id] == '_' || source[start_id] == ':')) {
                    start_id--;
                }
                callee_name = source.substr(start_id + 1, end_id - start_id);
                break;
            }
        } else if (c == ',' && paren_depth == 0) {
            comma_count++;
        }
    }

    if (callee_name.empty()) return JsonObject{};

    for (const auto* fn : prog.functions) {
        if (fn->name == callee_name || fn->name.ends_with("::" + callee_name)) {
            std::ostringstream oss;
            oss << fn->name << "(";
            JsonArray params_arr;
            for (size_t i = 0; i < fn->params.size(); ++i) {
                if (i > 0) oss << ", ";
                std::string param_label = TypeChecker::get_type_name(fn->params[i].type) + " " + std::string(fn->params[i].name);
                oss << param_label;
                JsonObject param_info;
                param_info["label"] = param_label;
                params_arr.push_back(param_info);
            }
            if (fn->is_variadic) {
                if (!fn->params.empty()) oss << ", ";
                oss << "...";
                JsonObject param_info;
                param_info["label"] = "...";
                params_arr.push_back(param_info);
            }
            oss << ") -> " << TypeChecker::get_type_name(fn->return_type);

            JsonObject sig;
            sig["label"] = oss.str();
            sig["parameters"] = params_arr;

            JsonObject result;
            result["signatures"] = JsonArray{sig};
            result["activeSignature"] = 0;
            result["activeParameter"] = comma_count;
            return result;
        }
    }

    return JsonObject{};
}

JsonArray LspServer::handle_document_symbols(const std::string& uri) {
    auto it = documents_.find(uri);
    if (it == documents_.end()) return JsonArray{};

    const std::string& source = it->second;
    std::string filepath = uri_to_path(uri);
    SourceManager sm(filepath, source);
    Diagnostics diag(sm, false);
    Arena arena;

    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);
    ASTProgram prog = parser.parse_program();

    JsonArray symbols;

    auto make_symbol = [&](std::string_view name, int kind, SourceSpan span) {
        auto lc = sm.get_line_col(span.start);
        uint32_t line_0 = lc.line > 0 ? lc.line - 1 : 0;
        uint32_t char_0 = lc.col > 0 ? lc.col - 1 : 0;

        JsonObject pos_start;
        pos_start["line"] = line_0;
        pos_start["character"] = char_0;

        JsonObject pos_end;
        pos_end["line"] = line_0;
        pos_end["character"] = char_0 + std::max(1u, span.length);

        JsonObject range;
        range["start"] = pos_start;
        range["end"] = pos_end;

        JsonObject sym;
        sym["name"] = std::string(name);
        sym["kind"] = kind;
        sym["range"] = range;
        sym["selectionRange"] = range;
        return sym;
    };

    // Functions (SymbolKind.Function = 12)
    for (const auto* fn : prog.functions) {
        symbols.push_back(make_symbol(fn->name, 12, fn->span));
    }
    // Structs (SymbolKind.Struct = 23)
    for (const auto* st : prog.structs) {
        symbols.push_back(make_symbol(st->name, 23, st->span));
    }
    // Unions (SymbolKind.Class = 5)
    for (const auto* un : prog.unions) {
        symbols.push_back(make_symbol(un->name, 5, un->span));
    }
    // Enums (SymbolKind.Enum = 10)
    for (const auto* en : prog.enums) {
        symbols.push_back(make_symbol(en->name, 10, en->span));
    }
    // Constants (SymbolKind.Constant = 14)
    for (const auto* c : prog.constants) {
        symbols.push_back(make_symbol(c->name, 14, c->span));
    }

    return symbols;
}

void LspServer::handle_request(const JsonValue& id, const std::string& method, const JsonValue& params) {
    if (method == "initialize") {
        JsonObject capabilities;
        capabilities["textDocumentSync"] = 1; // Full sync
        capabilities["hoverProvider"] = true;
        capabilities["definitionProvider"] = true;
        capabilities["referencesProvider"] = true;
        capabilities["documentHighlightProvider"] = true;
        capabilities["documentFormattingProvider"] = true;

        JsonObject rename_provider;
        rename_provider["prepareProvider"] = true;
        capabilities["renameProvider"] = rename_provider;
        
        JsonObject completion_provider;
        completion_provider["triggerCharacters"] = JsonArray{JsonValue("."), JsonValue(":")};
        capabilities["completionProvider"] = completion_provider;

        JsonObject sig_provider;
        sig_provider["triggerCharacters"] = JsonArray{JsonValue("("), JsonValue(",")};
        capabilities["signatureHelpProvider"] = sig_provider;

        capabilities["documentSymbolProvider"] = true;

        JsonObject server_info;
        server_info["name"] = "femto-lsp";
        server_info["version"] = "0.3.0";

        JsonObject result;
        result["capabilities"] = capabilities;
        result["serverInfo"] = server_info;
        send_response(id, result);
        return;
    }

    if (method == "shutdown") {
        send_response(id, nullptr);
        return;
    }

    if (method == "textDocument/hover") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        JsonObject hover = handle_hover(uri, line, col);
        send_response(id, hover.empty() ? JsonValue(nullptr) : JsonValue(hover));
        return;
    }

    if (method == "textDocument/definition") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        JsonValue def = handle_definition(uri, line, col);
        send_response(id, def);
        return;
    }

    if (method == "textDocument/references") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        bool include_decl = params["context"]["includeDeclaration"].as_bool();
        JsonArray refs = handle_references(uri, line, col, include_decl);
        send_response(id, refs);
        return;
    }

    if (method == "textDocument/documentHighlight") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        JsonArray hls = handle_document_highlights(uri, line, col);
        send_response(id, hls);
        return;
    }

    if (method == "textDocument/prepareRename") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        JsonValue prep = handle_prepare_rename(uri, line, col);
        send_response(id, prep);
        return;
    }

    if (method == "textDocument/rename") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        std::string new_name = params["newName"].as_string();
        JsonObject ren = handle_rename(uri, line, col, new_name);
        send_response(id, ren);
        return;
    }

    if (method == "textDocument/formatting") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t tab_size = (uint32_t)params["options"]["tabSize"].as_int();
        bool insert_spaces = params["options"]["insertSpaces"].as_bool();
        JsonArray edits = handle_formatting(uri, tab_size, insert_spaces);
        send_response(id, edits);
        return;
    }

    if (method == "textDocument/completion") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        JsonArray completions = handle_completion(uri, line, col);
        send_response(id, completions);
        return;
    }

    if (method == "textDocument/signatureHelp") {
        std::string uri = params["textDocument"]["uri"].as_string();
        uint32_t line = (uint32_t)params["position"]["line"].as_int();
        uint32_t col = (uint32_t)params["position"]["character"].as_int();
        JsonObject sig_help = handle_signature_help(uri, line, col);
        send_response(id, sig_help.empty() ? JsonValue(nullptr) : JsonValue(sig_help));
        return;
    }

    if (method == "textDocument/documentSymbol") {
        std::string uri = params["textDocument"]["uri"].as_string();
        JsonArray syms = handle_document_symbols(uri);
        send_response(id, syms);
        return;
    }

    send_response(id, nullptr);
}

void LspServer::handle_notification(const std::string& method, const JsonValue& params) {
    if (method == "exit") {
        running_ = false;
        return;
    }

    if (method == "textDocument/didOpen") {
        std::string uri = params["textDocument"]["uri"].as_string();
        std::string text = params["textDocument"]["text"].as_string();
        documents_[uri] = text;
        JsonObject res = analyze_document(uri, text);
        send_diagnostics(uri, res["diagnostics"].as_array());
        return;
    }

    if (method == "textDocument/didChange") {
        std::string uri = params["textDocument"]["uri"].as_string();
        const auto& changes = params["contentChanges"].as_array();
        if (!changes.empty()) {
            std::string text = changes.back()["text"].as_string();
            documents_[uri] = text;
            JsonObject res = analyze_document(uri, text);
            send_diagnostics(uri, res["diagnostics"].as_array());
        }
        return;
    }

    if (method == "textDocument/didSave") {
        std::string uri = params["textDocument"]["uri"].as_string();
        auto it = documents_.find(uri);
        if (it != documents_.end()) {
            JsonObject res = analyze_document(uri, it->second);
            send_diagnostics(uri, res["diagnostics"].as_array());
        }
        return;
    }

    if (method == "textDocument/didClose") {
        std::string uri = params["textDocument"]["uri"].as_string();
        documents_.erase(uri);
        send_diagnostics(uri, JsonArray{});
        return;
    }
}

void LspServer::process_message(const std::string& msg) {
    JsonValue json = JsonValue::parse(msg);
    if (!json.is_object()) return;

    std::string method = json["method"].as_string();
    if (json.contains("id")) {
        handle_request(json["id"], method, json["params"]);
    } else {
        handle_notification(method, json["params"]);
    }
}

void LspServer::run() {
    while (running_ && std::cin.good()) {
        std::string header_line;
        size_t content_length = 0;

        while (std::getline(std::cin, header_line)) {
            if (header_line == "\r" || header_line.empty()) {
                break;
            }
            if (header_line.rfind("Content-Length: ", 0) == 0) {
                try {
                    content_length = std::stoul(header_line.substr(16));
                } catch (...) {}
            }
        }

        if (content_length > 0) {
            std::string body(content_length, '\0');
            std::cin.read(&body[0], content_length);
            process_message(body);
        }
    }
}

} // namespace femto::lsp