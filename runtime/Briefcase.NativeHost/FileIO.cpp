#include "Startup.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <fstream>
#include <stdexcept>
namespace bc {
namespace fs = std::filesystem;
void assert_plain_path(const fs::path &path) {
    for (auto p = fs::absolute(path).lexically_normal(); !p.empty();) {
#ifdef _WIN32
        auto a = GetFileAttributesW(p.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Reparse point forbidden");
#else
        std::error_code error;
        auto status = fs::symlink_status(p, error);
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::system_error(error, "Cannot inspect path");
        if (fs::is_symlink(status))
            throw std::runtime_error("Symbolic link forbidden");
#endif
        auto parent = p.parent_path();
        if (parent == p)
            break;
        p = parent;
    }
}
std::string read_bounded(const fs::path &path, size_t limit) {
    assert_plain_path(path);
    if (fs::file_size(path) > limit)
        throw std::runtime_error("File size limit exceeded");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read file");
    std::string out((std::istreambuf_iterator<char>(in)), {});
    if (out.size() > limit || in.bad())
        throw std::runtime_error("Cannot read bounded file");
    return out;
}
} // namespace bc
