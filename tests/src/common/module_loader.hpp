#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

#include "common/arena.hpp"
#include "common/source_manager.hpp"
#include "common/diagnostic.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

namespace femto {

namespace fs = std::filesystem;

class ModuleLoader {
public:
    ModuleLoader(const std::vector<std::string>& search_paths, Arena& arena)
        : search_paths_(search_paths), arena_(arena) {}

    bool load_module_recursive(const std::string& filepath, const std::string& mod_prefix, ASTProgram& master_prog, Diagnostics& diag) {
        std::string canonical_path;
        try {
            if (!fs::exists(filepath)) {
                diag.report_error(SourceSpan{}, "file not found: '" + filepath + "'");
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
            diag.report_error(SourceSpan{}, "failed to open '" + canonical_path + "'");
            return false;
        }
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        return load_module_from_source(canonical_path, source, mod_prefix, master_prog, diag);
    }

    bool load_module_from_source(const std::string& filepath, const std::string& source, const std::string& mod_prefix, ASTProgram& master_prog, Diagnostics& diag) {
        auto* sm = arena_.allocate<SourceManager>(filepath, source);
        Lexer lexer(*sm, diag);
        Parser parser(lexer, arena_, diag);

        ASTProgram prog = parser.parse_program();
        if (diag.has_errors()) {
            return false;
        }

        for (const auto& imp : prog.imports) {
            std::string resolved_file = resolve_import_path(imp, filepath);
            if (resolved_file.empty()) {
                diag.report_error(SourceSpan{}, "cannot find module '" + imp + "' imported in '" + filepath + "'");
                return false;
            }
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

        for (auto* fn : prog.functions) {
            fn->file_path = filepath;
            if (!mod_prefix.empty() && fn->name.find("::") == std::string_view::npos) {
                if (fn->is_extern_c) {
                    master_prog.functions.push_back(fn);
                    std::string q_name = mod_prefix + "::" + std::string(fn->name);
                    char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
                    std::memcpy(q_buf, q_name.data(), q_name.size());
                    q_buf[q_name.size()] = '\0';

                    auto* alias_fn = arena_.allocate<ASTFunctionDecl>(*fn);
                    alias_fn->name = std::string_view(q_buf, q_name.size());
                    alias_fn->file_path = filepath;
                    master_prog.functions.push_back(alias_fn);
                } else {
                    std::string q_name = mod_prefix + "::" + std::string(fn->name);
                    char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
                    std::memcpy(q_buf, q_name.data(), q_name.size());
                    q_buf[q_name.size()] = '\0';
                    fn->name = std::string_view(q_buf, q_name.size());
                    master_prog.functions.push_back(fn);
                }
            } else {
                master_prog.functions.push_back(fn);
            }
        }
        for (auto* st : prog.structs) {
            st->file_path = filepath;
            master_prog.structs.push_back(st);
            if (!mod_prefix.empty() && st->name.find("::") == std::string_view::npos) {
                std::string q_name = mod_prefix + "::" + std::string(st->name);
                char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
                std::memcpy(q_buf, q_name.data(), q_name.size());
                q_buf[q_name.size()] = '\0';

                auto* alias_st = arena_.allocate<ASTStructDecl>(*st);
                alias_st->name = std::string_view(q_buf, q_name.size());
                alias_st->file_path = filepath;
                master_prog.structs.push_back(alias_st);
            }
        }
        for (auto* un : prog.unions) {
            un->file_path = filepath;
            master_prog.unions.push_back(un);
        }
        for (auto* en : prog.enums) {
            en->file_path = filepath;
            master_prog.enums.push_back(en);
        }
        for (auto* cn : prog.constants) {
            cn->file_path = filepath;
            master_prog.constants.push_back(cn);
        }

        return true;
    }

private:
    std::string resolve_import_path(const std::string& mod_name, const std::string& current_file) {
        std::string rel_file = mod_name + ".femto";

        fs::path cur_dir = fs::path(current_file).parent_path();
        if (fs::exists(cur_dir / rel_file)) {
            return (cur_dir / rel_file).string();
        }

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
    Arena& arena_;
};

} // namespace femto