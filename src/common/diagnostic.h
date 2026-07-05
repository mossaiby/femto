#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "source_location.h"

namespace femto {

enum class DiagnosticKind {
    Error,
    Warning,
    Note,
};

struct Diagnostic {
    DiagnosticKind kind;
    SourceLocation location;
    std::string message;
};

class DiagnosticEngine {
public:
    DiagnosticEngine(const std::string& filename, const std::string& source);

    void error(SourceLocation loc, const std::string& msg);
    void warning(SourceLocation loc, const std::string& msg);
    void note(SourceLocation loc, const std::string& msg);

    bool has_errors() const;
    std::size_t error_count() const;
    void print_all() const;

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    std::string filename_;
    const std::string& source_;
    std::vector<Diagnostic> diagnostics_;

    std::string get_line(SourceLocation loc) const;
    std::string format_message(const Diagnostic& d) const;
};

} // namespace femto
