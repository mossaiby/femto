#pragma once
#include <string_view>
#include <cstdint>
#include "common/source_manager.hpp"
#include "common/diagnostic.hpp"
#include "frontend/token.hpp"

namespace femto {

class Lexer {
public:
    Lexer(const SourceManager& sm, Diagnostics& diag)
        : sm_(sm), diag_(diag), src_(sm.source()), cursor_(0) {}

    Token next_token();
    std::string_view source() const { return src_; }
    const SourceManager& source_manager() const { return sm_; }

    uint32_t cursor() const { return cursor_; }
    void set_cursor(uint32_t pos) { cursor_ = pos <= src_.size() ? pos : (uint32_t)src_.size(); }

private:
    char peek(size_t ahead = 0) const {
        if (cursor_ + ahead >= src_.size()) return '\0';
        return src_[cursor_ + ahead];
    }

    char advance() {
        if (cursor_ >= src_.size()) return '\0';
        return src_[cursor_++];
    }

    void skip_whitespace_and_comments();
    Token scan_identifier_or_keyword();
    Token scan_number();
    Token scan_string();
    Token scan_raw_string();
    Token scan_char();

    const SourceManager& sm_;
    Diagnostics& diag_;
    std::string_view src_;
    uint32_t cursor_;
};

} // namespace femto