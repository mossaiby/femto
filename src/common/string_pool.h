#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace femto {

using StringId = std::uint32_t;

class StringPool {
public:
    static constexpr StringId NULL_ID = 0;

    StringId intern(const std::string& str);
    const std::string& get(StringId id) const;
    bool empty() const { return strings_.empty(); }
    std::size_t size() const { return strings_.size(); }

private:
    std::vector<std::string> strings_;
    std::unordered_map<std::string, StringId> map_;
};

} // namespace femto
