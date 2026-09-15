#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace bc {
struct ConnectParameter {
    std::wstring_view name;
    int32_t offset, size, array_dim;
    bool string, input;
};
inline bool direct_connect_contract(uint32_t size, const std::array<ConnectParameter, 2> &p) {
    return size == 32 && p[0].name == L"IPPort" && p[0].offset == 0 && p[0].size == 16 &&
           p[1].name == L"Password" && p[1].offset == 16 && p[1].size == 16 && p[0].array_dim == 1 &&
           p[1].array_dim == 1 && p[0].string && p[1].string && p[0].input && p[1].input;
}
} // namespace bc
