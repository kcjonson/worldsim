#pragma once

// Pure LRU slot allocator for the detail-page atlas. No GL, no threading — the
// eviction policy is unit-testable in isolation. A "key" is an opaque uint64
// (the renderer packs rhombus/pi/pj into it).

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace planetview {

class PlanetLru {
  public:
    static constexpr uint64_t kNoKey = ~0ULL;

    void init(int capacity) {
        capacity_ = capacity;
        slots_.assign(static_cast<size_t>(capacity), kNoKey);
        order_.clear();
        map_.clear();
        for (int i = 0; i < capacity; ++i) free_.push_back(i);
    }

    int capacity() const { return capacity_; }
    bool resident(uint64_t key) const { return map_.find(key) != map_.end(); }

    // Layer holding `key`, or -1 if not resident. Does NOT touch recency.
    int layerOf(uint64_t key) const {
        auto it = map_.find(key);
        return it == map_.end() ? -1 : it->second.layer;
    }

    // Mark `key` as most-recently-used (call when a resident page is visible).
    void touch(uint64_t key) {
        auto it = map_.find(key);
        if (it == map_.end()) return;
        order_.splice(order_.begin(), order_, it->second.it);
    }

    // Allocate a slot for `key`, evicting the least-recently-used resident page
    // if the atlas is full. Returns the atlas layer to fill, and (via outEvicted)
    // the key that was evicted (kNoKey if none). The caller must (re)bake the
    // page into the returned layer and update the evicted page's table entry.
    int allocate(uint64_t key, uint64_t& outEvicted) {
        outEvicted = kNoKey;
        auto existing = map_.find(key);
        if (existing != map_.end()) {
            order_.splice(order_.begin(), order_, existing->second.it);
            return existing->second.layer;
        }

        int layer;
        if (!free_.empty()) {
            layer = free_.back();
            free_.pop_back();
        } else {
            uint64_t victim = order_.back();
            auto vit = map_.find(victim);
            layer = vit->second.layer;
            outEvicted = victim;
            order_.pop_back();
            map_.erase(vit);
            slots_[static_cast<size_t>(layer)] = kNoKey;
        }

        order_.push_front(key);
        map_.emplace(key, Entry{layer, order_.begin()});
        slots_[static_cast<size_t>(layer)] = key;
        return layer;
    }

    // Drop everything (e.g. on snapshot / color-mode change).
    void clear() { init(capacity_); }

    uint64_t keyAt(int layer) const { return slots_[static_cast<size_t>(layer)]; }

  private:
    struct Entry {
        int layer;
        std::list<uint64_t>::iterator it;
    };
    int capacity_{0};
    std::vector<uint64_t> slots_;          // layer -> key (kNoKey if empty)
    std::list<uint64_t>   order_;          // front = MRU, back = LRU
    std::unordered_map<uint64_t, Entry> map_;
    std::vector<int>      free_;
};

} // namespace planetview
