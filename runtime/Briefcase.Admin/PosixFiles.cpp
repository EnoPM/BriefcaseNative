#include "Service.hpp"
#include "../Briefcase.NativeHost/Startup.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bc::admin {
namespace {
void need(bool ok, const char* code, const char* text) {
    if (!ok) throw Error(code, text);
}
struct File {
    int fd{-1};
    ~File() { if (fd >= 0) ::close(fd); }
};
void plain(int fd) {
    struct stat info{};
    need(fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1,
         "unsafe_path", "Redirected file refused.");
}
}
std::string read_file(const fs::path& path, size_t limit) {
    assert_plain_path(path);
    File file{::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
    need(file.fd >= 0, "file_unavailable", "File unavailable.");
    plain(file.fd);
    struct stat info{};
    need(fstat(file.fd, &info) == 0 && info.st_size >= 0 && uint64_t(info.st_size) <= limit,
         "file_too_large", "File is too large.");
    std::string out(size_t(info.st_size), 0);
    size_t offset = 0;
    while (offset < out.size()) {
        const auto count = ::read(file.fd, out.data() + offset, out.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        need(count > 0, "file_unavailable", "Incomplete read.");
        offset += size_t(count);
    }
    return out;
}
void write_file(const fs::path& path, std::string_view text, bool replace, bool private_file) {
    assert_plain_path(path.parent_path());
    if (fs::exists(path)) {
        need(replace, "already_configured", "The file already exists.");
        (void)read_file(path, 1048576);
    }
    const auto temporary = path.parent_path() /
        (path.filename().string() + "." + std::to_string(getpid()) + "." + hex(random_bytes(8)) + ".tmp");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code e; fs::remove(path, e); }
    } cleanup{temporary};
    File file{::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, private_file ? 0600 : 0644)};
    need(file.fd >= 0, "write_failed", "Unable to create temporary file.");
    if (private_file) {
        struct stat permissions{};
        need(fchmod(file.fd, 0600) == 0 && fstat(file.fd, &permissions) == 0 &&
             (permissions.st_mode & 0777) == 0600 && permissions.st_uid == geteuid(),
             "permissions", "Private administration files require Unix permissions (mode 600).");
    }
    size_t offset = 0;
    while (offset < text.size()) {
        const auto count = ::write(file.fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        need(count > 0, "write_failed", "Incomplete write; previous values preserved.");
        offset += size_t(count);
    }
    need(fsync(file.fd) == 0, "write_failed", "Unable to flush temporary file.");
    assert_plain_path(path.parent_path());
    if (fs::exists(path)) (void)read_file(path, 1048576);
    if (replace) {
        need(::rename(temporary.c_str(), path.c_str()) == 0, "write_failed", "Atomic save failed.");
    } else {
        // link is an atomic create-if-absent; unlike exists()+rename it cannot overwrite a racer.
        need(::link(temporary.c_str(), path.c_str()) == 0, "write_failed", "Atomic creation failed.");
        need(::unlink(temporary.c_str()) == 0, "write_failed", "Temporary link cleanup failed.");
    }
    File directory{::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    need(directory.fd >= 0 && fsync(directory.fd) == 0, "write_failed", "Unable to flush directory.");
}
} // namespace bc::admin
