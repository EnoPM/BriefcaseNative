#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>
namespace bc {
template <class Object, class Guard = Object *> class HandleTable {
    struct Key {
        uint64_t owner;
        Object *object;
        bool operator==(const Key &) const = default;
    };
    struct Hash {
        size_t operator()(const Key &k) const {
            return std::hash<void *>{}(k.object) ^ (k.owner * 0x9e3779b97f4a7c15ull);
        }
    };
    struct Entry {
        uint64_t owner;
        Object *object;
        Guard guard;
        bool guard_available;
        uint32_t refs = 1;
        bool canonical = false;
    };
    std::unordered_map<uint64_t, Entry> entries_;
    std::unordered_map<Key, uint64_t, Hash> canonical_;
    std::mutex mutex_;
    uint64_t next_ = 1;
    size_t limit_;
    static Guard capture(Object *object) {
        if constexpr (std::is_pointer_v<Guard>) return object;
        else {
            Guard guard{};
            guard = object;
            return guard;
        }
    }
    static Object *resolve(const Entry &entry) {
        if constexpr (std::is_pointer_v<Guard>) return entry.object;
        else return entry.guard_available ? entry.guard.Get() : entry.object;
    }
    std::optional<uint64_t> add(uint64_t owner, Object *object, bool canonical) {
        if (!owner || !object || !next_ || entries_.size() >= limit_)
            return {};
        auto guard = capture(object);
        // Some Unreal objects have no allocated weak serial yet. Their lifetime
        // is still tracked by the UObject deletion listener, so retain the raw
        // address until that listener revokes this entry.
        bool guard_available = true;
        if constexpr (!std::is_pointer_v<Guard>) guard_available = guard.Get() != nullptr;
        auto token = next_++;
        entries_.emplace(token, Entry{owner, object, guard, guard_available, 1, canonical});
        if (canonical)
            canonical_[{owner, object}] = token;
        return token;
    }

  public:
    struct Revoked {
        uint64_t owner, token;
    };
    explicit HandleTable(size_t limit = 4096) : limit_(limit) {}
    std::optional<uint64_t> insert(uint64_t owner, Object *object) {
        std::lock_guard lock(mutex_);
        return add(owner, object, false);
    }
    std::optional<uint64_t> intern(uint64_t owner, Object *object) {
        std::lock_guard lock(mutex_);
        auto i = canonical_.find({owner, object});
        if (i == canonical_.end())
            return add(owner, object, true);
        auto entry = entries_.find(i->second);
        if (entry != entries_.end() && resolve(entry->second) == object) {
            if (entry->second.refs == UINT32_MAX)
                return {};
            ++entry->second.refs;
            return i->second;
        }
        if (entry != entries_.end()) entries_.erase(entry);
        canonical_.erase(i);
        return add(owner, object, true);
    }
    bool retain(uint64_t owner, uint64_t token) {
        std::lock_guard lock(mutex_);
        auto i = entries_.find(token);
        if (i == entries_.end() || i->second.owner != owner || i->second.refs == UINT32_MAX ||
            !resolve(i->second))
            return false;
        ++i->second.refs;
        return true;
    }
    Object *get(uint64_t owner, uint64_t token) {
        std::lock_guard lock(mutex_);
        auto i = entries_.find(token);
        return i != entries_.end() && i->second.owner == owner ? resolve(i->second) : nullptr;
    }
    template <class Predicate> bool valid(uint64_t owner, uint64_t token, Predicate predicate) {
        std::lock_guard lock(mutex_);
        auto i = entries_.find(token);
        if (i == entries_.end() || i->second.owner != owner) return false;
        auto *object = resolve(i->second);
        return object && predicate(object);
    }
    bool release(uint64_t owner, uint64_t token) {
        std::lock_guard lock(mutex_);
        auto i = entries_.find(token);
        if (i == entries_.end() || i->second.owner != owner)
            return false;
        if (--i->second.refs)
            return true;
        if (i->second.canonical)
            canonical_.erase({owner, i->second.object});
        entries_.erase(i);
        return true;
    }
    std::vector<Revoked> invalidate_collect(const void *object) {
        std::lock_guard lock(mutex_);
        std::vector<Revoked> result;
        for (auto i = entries_.begin(); i != entries_.end();) {
            if (static_cast<const void *>(i->second.object) == object) {
                result.push_back({i->second.owner, i->first});
                if (i->second.canonical)
                    canonical_.erase({i->second.owner, i->second.object});
                i = entries_.erase(i);
            } else
                ++i;
        }
        return result;
    }
    void invalidate(const void *object) { invalidate_collect(object); }
    void erase_owner(uint64_t owner) {
        std::lock_guard lock(mutex_);
        for (auto i = entries_.begin(); i != entries_.end();) {
            if (i->second.owner == owner) {
                if (i->second.canonical)
                    canonical_.erase({owner, i->second.object});
                i = entries_.erase(i);
            } else
                ++i;
        }
    }
    void clear() {
        std::lock_guard lock(mutex_);
        entries_.clear();
        canonical_.clear();
    }
};
} // namespace bc
