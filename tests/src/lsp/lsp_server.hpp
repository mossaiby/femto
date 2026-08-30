#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include "lsp/json.hpp"
#include "common/arena.hpp"
#include "common/source_manager.hpp"
#include "common/diagnostic.hpp"
#include "frontend/ast.hpp"
#include "sema/type_checker.hpp"

namespace femto::lsp {

class LspServer {
public:
    LspServer();

    void run();

    void set_search_paths(std::vector<std::string> paths) {
        search_paths_ = std::move(paths);
    }

    void open_document(const std::string& uri, std::string source) {
        documents_[uri] = std::move(source);
    }

    // Document analysis & query handlers
    JsonObject analyze_document(const std::string& uri, const std::string& source);
    JsonObject handle_hover(const std::string& uri, uint32_t line_0, uint32_t char_0);
    JsonValue  handle_definition(const std::string& uri, uint32_t line_0, uint32_t char_0);
    JsonArray  handle_completion(const std::string& uri, uint32_t line_0, uint32_t char_0);
    JsonObject handle_signature_help(const std::string& uri, uint32_t line_0, uint32_t char_0);
    JsonArray  handle_document_symbols(const std::string& uri);
    JsonArray  handle_references(const std::string& uri, uint32_t line_0, uint32_t char_0, bool include_decl);
    JsonArray  handle_document_highlights(const std::string& uri, uint32_t line_0, uint32_t char_0);
    JsonValue  handle_prepare_rename(const std::string& uri, uint32_t line_0, uint32_t char_0);
    JsonObject handle_rename(const std::string& uri, uint32_t line_0, uint32_t char_0, const std::string& new_name);
    JsonArray  handle_formatting(const std::string& uri, uint32_t tab_size, bool insert_spaces);

private:
    void process_message(const std::string& msg);
    void handle_request(const JsonValue& id, const std::string& method, const JsonValue& params);
    void handle_notification(const std::string& method, const JsonValue& params);

    void send_response(const JsonValue& id, const JsonValue& result);
    void send_notification(const std::string& method, const JsonValue& params);
    void send_diagnostics(const std::string& uri, const JsonArray& diagnostics);

    std::string uri_to_path(const std::string& uri);

    bool running_ = true;
    std::vector<std::string> search_paths_;
    std::unordered_map<std::string, std::string> documents_;
};

} // namespace femto::lsp