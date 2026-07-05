#include "string_pool.h"

namespace femto {

StringId StringPool::intern(const std::string& str) {
    auto it = map_.find(str);
    if (it != map_.end()) return it->second;

    StringId id = static_cast<StringId>(strings_.size());
    strings_.push_back(str);
    map_[str] = id;
    return id;
}

const std::string& StringPool::get(StringId id) const {
    return strings_.at(id);
}

} // namespace femto
