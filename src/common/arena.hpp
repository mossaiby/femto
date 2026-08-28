#pragma once
#include <memory_resource>
#include <cstddef>
#include <utility>

namespace femto {

class Arena {
public:
    explicit Arena(size_t initial_size = 1024 * 1024) 
        : pool_(initial_size, std::pmr::new_delete_resource()) {}

    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
        void* mem = pool_.allocate(sizeof(T), alignof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    std::pmr::memory_resource* resource() { return &pool_; }

private:
    std::pmr::monotonic_buffer_resource pool_;
};

} // namespace femto