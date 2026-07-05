#include "diagnostic.h"

#include <cstdio>
#include <sstream>

namespace femto {

DiagnosticEngine::DiagnosticEngine(const std::string& filename, const std::string& source)
    : filename_(filename), source_(source) {}

void DiagnosticEngine::error(SourceLocation loc, const std::string& msg) {
    diagnostics_.push_back({DiagnosticKind::Error, loc, msg});
}

void DiagnosticEngine::warning(SourceLocation loc, const std::string& msg) {
    diagnostics_.push_back({DiagnosticKind::Warning, loc, msg});
}

void DiagnosticEngine::note(SourceLocation loc, const std::string& msg) {
    diagnostics_.push_back({DiagnosticKind::Note, loc, msg});
}

bool DiagnosticEngine::has_errors() const {
    for (const auto& d : diagnostics_) {
        if (d.kind == DiagnosticKind::Error) return true;
    }
    return false;
}

std::size_t DiagnosticEngine::error_count() const {
    std::size_t count = 0;
    for (const auto& d : diagnostics_) {
        if (d.kind == DiagnosticKind::Error) ++count;
    }
    return count;
}

std::string DiagnosticEngine::get_line(SourceLocation loc) const {
    std::uint32_t line_num = 1;
    std::uint32_t col = 0;
    for (char c : source_) {
        if (line_num == loc.line) {
            if (c == '\n') break;
            ++col;
        }
        if (c == '\n') {
            ++line_num;
            col = 0;
        }
    }

    std::istringstream stream(source_);
    std::string line;
    std::uint32_t current = 1;
    while (std::getline(stream, line)) {
        if (current == loc.line) return line;
        ++current;
    }
    return "";
}

std::string DiagnosticEngine::format_message(const Diagnostic& d) const {
    const char* kind_str = "";
    switch (d.kind) {
        case DiagnosticKind::Error:   kind_str = "error"; break;
        case DiagnosticKind::Warning: kind_str = "warning"; break;
        case DiagnosticKind::Note:    kind_str = "note"; break;
    }

    std::ostringstream oss;
    oss << filename_ << ":" << d.location.line << ":" << d.location.column
        << ": " << kind_str << ": " << d.message << "\n";

    std::string line_text = get_line(d.location);
    if (!line_text.empty()) {
        oss << line_text << "\n";
        for (std::uint32_t i = 1; i < d.location.column; ++i) {
            oss << ' ';
        }
        oss << "^\n";
    }

    return oss.str();
}

void DiagnosticEngine::print_all() const {
    for (const auto& d : diagnostics_) {
        std::fputs(format_message(d).c_str(), stderr);
    }
}

} // namespace femto
