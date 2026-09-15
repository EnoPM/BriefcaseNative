#pragma once
#include <stdexcept>
#include <string_view>
#include <vector>
namespace bc {
inline std::vector<std::string_view> property_path(std::string_view path) {
    if (path.empty() || path.size() > 256)
        throw std::runtime_error("Invalid property path");
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        auto n = path.find('.');
        auto part = path.substr(0, n);
        if (part.empty() || part.size() > 64 || parts.size() >= 8)
            throw std::runtime_error("Property path depth/segment limit");
        for (auto c : part)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                throw std::runtime_error("Invalid property name");
        parts.push_back(part);
        if (n == std::string_view::npos)
            break;
        path.remove_prefix(n + 1);
        if (path.empty())
            throw std::runtime_error("Empty property path segment");
    }
    return parts;
}
} // namespace bc
