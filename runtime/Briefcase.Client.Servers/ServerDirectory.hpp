#pragma once
#include <Briefcase/ModApi.h>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace bc::servers {
struct Entry {
    uint64_t id{};
    std::string name, endpoint;
};
struct Snapshot {
    std::vector<Entry> entries;
    std::string message;
    bool pending{}, writable{true};
};
std::string normalize_endpoint(std::string_view);
std::string normalize_name(std::string_view);
// Disk operations belong exclusively to the host worker; UI calls only enqueue or copy snapshots.
class Directory {
    std::filesystem::path path_;
    mutable std::mutex mutex_;
    Snapshot state_;
    struct Operation {
        bool add;
        Entry entry;
    };
    std::deque<Operation> queue_;
    uint64_t next_id_{1};
    void save(const std::vector<Entry> &, uint64_t next_id);

  public:
    void load(const std::filesystem::path &);
    Snapshot snapshot() const;
    BcResult add(std::string_view name, std::string_view endpoint);
    BcResult remove(uint64_t id);
    std::optional<Entry> find(uint64_t id) const;
    std::string process_one();
};
} // namespace bc::servers
