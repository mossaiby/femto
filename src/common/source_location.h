#pragma once

#include <cstdint>
#include <string>

namespace femto {

struct SourceLocation {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::uint32_t offset = 0;

    bool operator==(const SourceLocation& other) const = default;
    bool operator!=(const SourceLocation& other) const = default;
    bool operator<(const SourceLocation& other) const { return offset < other.offset; }
};

struct SourceSpan {
    SourceLocation start;
    SourceLocation end;

    std::uint32_t length() const { return end.offset - start.offset; }
};

} // namespace femto
