#include "LinuxUpdate.hpp"
#include <array>
#include <fcntl.h>
#include <mbedtls/sha256.h>
#include <regex>
#include <set>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bc::launcher {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
namespace {
struct Fd { int value; ~Fd() { if (value >= 0) close(value); } };
void regular(int fd, uint64_t limit) {
    struct stat info{};
    require(fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
            info.st_size >= 0 && uint64_t(info.st_size) <= limit, "Unsafe or oversized file");
}
std::string hex(const unsigned char* bytes, size_t size) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < size; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}
void sync_directory(const fs::path& path) {
    Fd fd{open(plain(path).c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    require(fd.value >= 0 && fsync(fd.value) == 0, "Cannot sync directory");
}
}
fs::path plain(const fs::path& input) {
    const auto result = fs::absolute(input);
    fs::path prefix;
    for (const auto& part : result) {
        require(part != "..", "Parent traversal refused");
        prefix /= part;
        std::error_code error;
        auto status = fs::symlink_status(prefix, error);
        require(!error || error == std::errc::no_such_file_or_directory, "Cannot inspect path");
        require(!fs::is_symlink(status), "Symbolic link refused");
    }
    return result.lexically_normal();
}
fs::path package_path(const fs::path& root, const std::string& name) {
    require(!name.empty() && name.front() != '/' && name.back() != '/' &&
            std::regex_match(name, std::regex("[A-Za-z0-9_./-]+")), "Invalid package path");
    size_t start = 0;
    do {
        auto end = name.find('/', start);
        auto part = name.substr(start, end == std::string::npos ? end : end - start);
        require(!part.empty() && part != "." && part != "..", "Unsafe package path");
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    return plain(root / name);
}
bool managed(const std::string& name, bool legacy) {
    static const std::set<std::string> required{
        "Briefcase.ServerLauncher", "Package.json", "Briefcase/Runtime/libBriefcase.NativeHost.so",
        "Briefcase/Runtime/libBriefcase.ServerBootstrap.so", "Briefcase/Tools/Briefcase.AdminSetup",
        "Briefcase/Updater/build.json", "Briefcase/Updater/updater.example.json"};
    static const std::set<std::string> retired{"StartBriefcaseNativeServer.sh", "Briefcase/Updater/supervisor.py", "Briefcase/Updater/updater.py"};
    return required.contains(name) || (legacy && retired.contains(name)) ||
        std::regex_match(name, std::regex("Briefcase/(Docs|Licenses|Localization)/[A-Za-z0-9_./-]+\\.(json|txt|md)"));
}
std::string read(const fs::path& path, uint64_t limit) {
    Fd fd{open(plain(path).c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
    regular(fd.value, limit);
    std::string result;
    std::array<char, 65536> buffer{};
    while (true) {
        auto count = ::read(fd.value, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        require(count >= 0, "Cannot read file");
        if (!count) break;
        require(result.size() + count <= limit, "File exceeds limit");
        result.append(buffer.data(), count);
    }
    return result;
}
Json document(const fs::path& path) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(read(path), [&](int, Json::parse_event_t event, Json& value) {
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key) require(keys.back().insert(value.get<std::string>()).second, "Duplicate JSON key");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
std::string digest(const fs::path& path) {
    Fd fd{open(plain(path).c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
    regular(fd.value, UINT64_MAX);
    struct Hash { mbedtls_sha256_context context; Hash() { mbedtls_sha256_init(&context); } ~Hash() { mbedtls_sha256_free(&context); } } hash;
    require(mbedtls_sha256_starts(&hash.context, 0) == 0, "SHA-256 initialization failed");
    std::array<unsigned char, 65536> buffer{};
    while (true) {
        auto count = ::read(fd.value, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        require(count >= 0, "Hash read failed");
        if (!count) break;
        require(mbedtls_sha256_update(&hash.context, buffer.data(), count) == 0, "SHA-256 update failed");
    }
    unsigned char output[32];
    require(mbedtls_sha256_finish(&hash.context, output) == 0, "SHA-256 finish failed");
    return hex(output, sizeof(output));
}
std::string identifier() {
    unsigned char bytes[16]; size_t count = 0;
    while (count < sizeof(bytes)) {
        auto got = getrandom(bytes + count, sizeof(bytes) - count, 0);
        if (got < 0 && errno == EINTR) continue;
        require(got > 0, "Random source failed"); count += got;
    }
    return hex(bytes, sizeof(bytes));
}
void atomic(const fs::path& path, const std::string& data, unsigned mode) {
    plain(path); fs::create_directories(path.parent_path()); plain(path);
    if (fs::exists(path)) { Fd old{open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)}; regular(old.value, max_archive); }
    const auto temporary = path.parent_path() / ("." + path.filename().string() + "." + identifier());
    Fd fd{open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    require(fd.value >= 0, "Cannot create atomic file");
    try {
        size_t offset = 0;
        while (offset < data.size()) {
            auto count = write(fd.value, data.data() + offset, data.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            require(count > 0, "File write failed"); offset += count;
        }
        require(fchmod(fd.value, mode) == 0 && fsync(fd.value) == 0, "Cannot sync file");
        plain(path); fs::rename(temporary, path); sync_directory(path.parent_path());
    } catch (...) { std::error_code error; fs::remove(temporary, error); throw; }
}
void write_json(const fs::path& path, const Json& value) { atomic(path, value.dump(2) + "\n"); }
// Used only after validating managed paths and regular files.
void remove_file(const fs::path& path) {
    if (!fs::exists(plain(path))) return;
    Fd fd{open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
    regular(fd.value, max_archive);
    require(unlink(path.c_str()) == 0, "Cannot remove obsolete file");
    sync_directory(path.parent_path());
}
}
