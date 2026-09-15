#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
namespace bc {
// Game-thread only. Snapshot dispatch supports self-removal and nested calls.
template <class Payload> class HookRegistry {
    struct Entry {
        uint64_t id, owner, target;
        uint32_t phase;
        bool alive = true, busy = false;
        std::function<void(Payload &)> callback;
    };
    std::unordered_map<uint64_t, std::shared_ptr<Entry>> entries_;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<Entry>>> targets_;
    uint64_t next_ = uint64_t(1) << 63;
    size_t limit_;

  public:
    explicit HookRegistry(size_t limit = 512) : limit_(limit) {}
    uint64_t add(uint64_t owner, uint64_t target, uint32_t phase, std::function<void(Payload &)> cb) {
        if (!owner || !target || !cb || (phase != 1 && phase != 2) || entries_.size() >= limit_ || !next_)
            return 0;
        size_t owned = 0;
        for (const auto &[id, e] : entries_)
            if (e->owner == owner)
                ++owned;
        if (owned >= 64)
            return 0;
        auto e = std::make_shared<Entry>(Entry{next_++, owner, target, phase, true, false, std::move(cb)});
        entries_.emplace(e->id, e);
        targets_[target].push_back(e);
        return e->id;
    }
    bool erase(uint64_t owner, uint64_t id) {
        const auto it = entries_.find(id);
        if (it == entries_.end() || it->second->owner != owner)
            return false;
        auto e = it->second;
        e->alive = false;
        auto target = targets_.find(e->target);
        std::erase_if(target->second, [&](auto &item) { return item->id == id; });
        if (target->second.empty())
            targets_.erase(target);
        entries_.erase(it);
        return true;
    }
    void erase_owner(uint64_t owner) {
        std::vector<uint64_t> ids;
        for (const auto &[id, e] : entries_)
            if (e->owner == owner)
                ids.push_back(id);
        for (auto id : ids)
            erase(owner, id);
    }
    bool contains(uint64_t target) const { return targets_.contains(target); }
    size_t size() const { return entries_.size(); }
    // An exception disables only that registration, never the vanilla call.
    uint32_t dispatch(uint64_t target, uint32_t phase, Payload &payload) noexcept {
        const auto it = targets_.find(target);
        if (it == targets_.end())
            return 0;
        uint32_t failures = 0;
        try {
            const auto snapshot = it->second;
            for (const auto &e : snapshot)
                if (e->alive && e->phase == phase && !e->busy) {
                    e->busy = true;
                    try {
                        e->callback(payload);
                    } catch (...) {
                        e->alive = false;
                        ++failures;
                    }
                    e->busy = false;
                }
        } catch (...) {
            ++failures;
        }
        return failures;
    }
};
} // namespace bc
