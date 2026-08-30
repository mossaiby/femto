#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace femto {

struct SourceLocation {
    uint32_t offset = 0;
};

struct SourceSpan {
    SourceLocation start;
    uint32_t length = 0;

    SourceSpan merge(SourceSpan other) const {
        uint32_t new_start = (std::min)(start.offset, other.start.offset);
        uint32_t new_end   = (std::max)(start.offset + length, other.start.offset + other.length);
        return SourceSpan{ SourceLocation{new_start}, new_end - new_start };
    }
};

class SourceManager {
public:
    SourceManager(std::string filename, std::string source)
        : filename_(std::move(filename)), source_(std::move(source)) {
        compute_line_offsets();
    }

    std::string_view source() const { return source_; }
    std::string_view filename() const { return filename_; }

    struct LineCol {
        uint32_t line;
        uint32_t col;
        std::string_view line_slice;
    };

    LineCol get_line_col(SourceLocation loc) const {
        uint32_t offset = loc.offset;
        if (line_offsets_.empty()) return { 1, 1, source_ };
        uint32_t l = 0, r = static_cast<uint32_t>(line_offsets_.size()) - 1;
        while (l <= r) {
            uint32_t mid = l + (r - l) / 2;
            if (line_offsets_[mid] <= offset) {
                if (mid + 1 == line_offsets_.size() || line_offsets_[mid + 1] > offset) {
                    uint32_t line_start = line_offsets_[mid];
                    uint32_t line_end = (mid + 1 < line_offsets_.size()) ? line_offsets_[mid + 1] - 1 : source_.size();
                    return { mid + 1, offset - line_start + 1, std::string_view(source_.data() + line_start, line_end - line_start) };
                }
                l = mid + 1;
            } else {
                if (mid == 0) break;
                r = mid - 1;
            }
        }
        return { 1, 1, source_ };
    }

private:
    void compute_line_offsets() {
        line_offsets_.push_back(0);
        for (uint32_t i = 0; i < source_.size(); ++i) {
            if (source_[i] == '\n') {
                line_offsets_.push_back(i + 1);
            }
        }
    }

    std::string filename_;
    std::string source_;
    std::vector<uint32_t> line_offsets_;
};

} // namespace femto