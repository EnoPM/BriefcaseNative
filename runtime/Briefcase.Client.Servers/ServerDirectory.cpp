#include "ServerDirectory.hpp"
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace bc::servers {
static std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == input.npos)
        return {};
    return std::string(input.substr(first, input.find_last_not_of(" \t\r\n") - first + 1));
}
std::string normalize_name(std::string_view input) {
    auto value = trim(input);
    if (value.empty() || value.size() > 95 ||
        std::ranges::any_of(value, [](unsigned char c) { return c < 32 || c == 127; }) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0))
        throw std::runtime_error("A name is required, up to 95 UTF-8 bytes.");
    return value;
}
std::string normalize_endpoint(std::string_view input) {
    auto value = trim(input);
    if (value.empty() || value.size() > 255)
        throw std::runtime_error("An address is required: host:port.");
    std::string host, port;
    if (value.front() == '[') {
        const auto end = value.find(']');
        if (end == value.npos || end + 2 >= value.size() || value[end + 1] != ':')
            throw std::runtime_error("For IPv6, use [address]:port.");
        IN6_ADDR address{};
        const auto inside = value.substr(1, end - 1);
        if (InetPtonA(AF_INET6, inside.c_str(), &address) != 1)
            throw std::runtime_error("Invalid IPv6 address.");
        char canonical[64]{};
        if (!InetNtopA(AF_INET6, &address, canonical, sizeof(canonical)))
            throw std::runtime_error("Invalid IPv6 address.");
        host = "[" + std::string(canonical) + "]";
        port = value.substr(end + 2);
    } else {
        const auto colon = value.find(':');
        if (colon == value.npos || colon == 0 || value.find(':', colon + 1) != value.npos)
            throw std::runtime_error("Expected host:port or [IPv6]:port.");
        host = value.substr(0, colon);
        port = value.substr(colon + 1);
        for (auto &c : host)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        if (host.size() > 253 || host.back() == '.' || std::ranges::any_of(host, [](unsigned char c) {
                return !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.');
            }))
            throw std::runtime_error("Invalid host name.");
        size_t start = 0;
        do {
            const auto end = host.find('.', start);
            const auto length = (end == host.npos ? host.size() : end) - start;
            if (!length || length > 63 || host[start] == '-' || host[start + length - 1] == '-')
                throw std::runtime_error("Invalid host name.");
            if (end == host.npos)
                break;
            start = end + 1;
        } while (true);
        if (std::ranges::all_of(host, [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
            IN_ADDR address{};
            if (InetPtonA(AF_INET, host.c_str(), &address) != 1)
                throw std::runtime_error("Invalid IPv4 address.");
        }
    }
    unsigned number{};
    auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || !number || number > 65535)
        throw std::runtime_error("Port must be between 1 and 65535.");
    return host + ":" + std::to_string(number);
}
static void plain_path(const std::filesystem::path &path) {
    for (auto p = std::filesystem::absolute(path); !p.empty();) {
        const auto attrs = GetFileAttributesW(p.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Links and junctions are not allowed for the server list.");
        const auto parent = p.parent_path();
        if (parent == p)
            break;
        p = parent;
    }
}
void Directory::load(const std::filesystem::path &path) {
    Snapshot next;
    uint64_t id = 1;
    try {
        plain_path(path);
        if (std::filesystem::exists(path)) {
            if (std::filesystem::file_size(path) > 65536)
                throw std::runtime_error("File is too large.");
            std::ifstream file(path, std::ios::binary);
            if (!file)
                throw std::runtime_error("Unable to read the file.");
            auto j = nlohmann::json::parse(file);
            if (j.at("version") != 1 || !j.at("servers").is_array() || j.at("servers").size() > 64)
                throw std::runtime_error("Unsupported server list format.");
            id = j.at("nextId").get<uint64_t>();
            std::set<uint64_t> ids;
            std::set<std::string> endpoints;
            for (const auto &item : j.at("servers")) {
                Entry e{item.at("id").get<uint64_t>(), normalize_name(item.at("name").get<std::string>()),
                        normalize_endpoint(item.at("endpoint").get<std::string>())};
                if (!e.id || e.id >= id || !ids.insert(e.id).second || !endpoints.insert(e.endpoint).second)
                    throw std::runtime_error("Duplicate identifier or address.");
                next.entries.push_back(std::move(e));
            }
            if (!id)
                throw std::runtime_error("Invalid identifier.");
        }
    } catch (const std::exception &e) {
        next = {};
        next.writable = false;
        next.message = std::string("List not loaded (file preserved): ") + e.what();
    }
    std::lock_guard lock(mutex_);
    path_ = path;
    state_ = std::move(next);
    next_id_ = id;
    queue_.clear();
}
Snapshot Directory::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}
std::optional<Entry> Directory::find(uint64_t id) const {
    std::lock_guard lock(mutex_);
    for (const auto &entry : state_.entries)
        if (entry.id == id)
            return entry;
    return {};
}
BcResult Directory::add(std::string_view name, std::string_view endpoint) {
    Entry e;
    try {
        e.name = normalize_name(name);
        e.endpoint = normalize_endpoint(endpoint);
    } catch (const std::exception &ex) {
        std::lock_guard lock(mutex_);
        state_.message = ex.what();
        return BC_INVALID_ARGUMENT;
    }
    std::lock_guard lock(mutex_);
    if (!state_.writable)
        return BC_DENIED;
    if (state_.pending)
        return BC_NOT_READY;
    if (state_.entries.size() >= 64 || next_id_ == UINT64_MAX) {
        state_.message = "The list is limited to 64 servers.";
        return BC_LIMIT;
    }
    for (const auto &entry : state_.entries)
        if (entry.endpoint == e.endpoint) {
            state_.message = "This address is already saved.";
            return BC_INVALID_ARGUMENT;
        }
    e.id = next_id_;
    queue_.push_back({true, std::move(e)});
    state_.pending = true;
    state_.message = "Saving...";
    return BC_OK;
}
BcResult Directory::remove(uint64_t id) {
    std::lock_guard lock(mutex_);
    if (!state_.writable)
        return BC_DENIED;
    if (state_.pending)
        return BC_NOT_READY;
    if (std::ranges::none_of(state_.entries, [id](const Entry &e) { return e.id == id; }))
        return BC_NOT_FOUND;
    queue_.push_back({false, {id}});
    state_.pending = true;
    state_.message = "Removing...";
    return BC_OK;
}
void Directory::save(const std::vector<Entry> &entries, uint64_t next_id) {
    plain_path(path_);
    auto temporary = path_;
    temporary += L".tmp";
    plain_path(temporary);
    auto j = nlohmann::json{{"version", 1}, {"nextId", next_id}, {"servers", nlohmann::json::array()}};
    for (const auto &e : entries)
        j["servers"].push_back({{"id", e.id}, {"name", e.name}, {"endpoint", e.endpoint}});
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file << j.dump(2);
        file.flush();
        file.close();
    }
    if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Unable to replace the file.");
}
std::string Directory::process_one() {
    Operation op;
    std::vector<Entry> next;
    uint64_t id;
    {
        std::lock_guard lock(mutex_);
        if (queue_.empty())
            return {};
        op = std::move(queue_.front());
        queue_.pop_front();
        next = state_.entries;
        id = next_id_;
    }
    std::string message;
    bool success = false;
    try {
        if (op.add) {
            next.push_back(op.entry);
            ++id;
        } else
            std::erase_if(next, [&](const Entry &e) { return e.id == op.entry.id; });
        save(next, id);
        success = true;
        message = op.add ? "Server saved." : "Server removed.";
    } catch (const std::exception &e) {
        message = std::string("Save failed, list unchanged: ") + e.what();
    }
    {
        std::lock_guard lock(mutex_);
        if (success) {
            state_.entries = std::move(next);
            next_id_ = id;
        }
        state_.pending = false;
        state_.message = message;
    }
    return message;
}
} // namespace bc::servers
