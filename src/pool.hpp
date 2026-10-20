#pragma once

#include <vector>

namespace kiln {

// Bounded recycle lists. The slot vector is intentionally leaked so thread
// teardown does not destroy pooled values while another pool is already gone.
template <typename T>
struct Pool {
    static std::vector<T*>& slots() noexcept {
        thread_local std::vector<T*>* items = new std::vector<T*>();
        return *items;
    }

    static T* take() {
        auto& items = slots();
        if (!items.empty()) {
            T* pointer = items.back();
            items.pop_back();
            return pointer;
        }
        return new T();
    }

    static void give(T* pointer) noexcept {
        if (pointer == nullptr) {
            return;
        }
        try {
            auto& items = slots();
            if (items.size() < 32) {
                items.push_back(pointer);
                return;
            }
        } catch (...) {
        }
        delete pointer;
    }
};

}  // namespace kiln
