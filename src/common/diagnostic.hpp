#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include "common/source_manager.hpp"

namespace femto {

class Diagnostics {
public:
    explicit Diagnostics(const SourceManager& sm) : sm_(sm) {}

    void report_error(SourceSpan span, std::string_view message) {
        has_errors_ = true;
        auto lc = sm_.get_line_col(span.start);
        std::cerr << "\033[1;31merror\033[0m: " << message 
                  << " \033[90m--> " << sm_.filename() << ":" << lc.line << ":" << lc.col << "\033[0m\n";
        std::cerr << "   \033[94m|\033[0m\n";
        std::cerr << (lc.line < 10 ? " " : "") << lc.line << " \033[94m|\033[0m " << lc.line_slice << "\n";
        
        std::string underline = std::string(lc.col > 0 ? lc.col - 1 : 0, ' ') + 
                                std::string(std::max(1u, span.length), '^');
        std::cerr << "   \033[94m|\033[0m \033[1;31m" << underline << "\033[0m\n";
    }

    bool has_errors() const { return has_errors_; }

private:
    const SourceManager& sm_;
    bool has_errors_ = false;
};

} // namespace femto