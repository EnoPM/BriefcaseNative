#include "LinuxUpdate.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <chrono>
#include <curl/curl.h>
#include <fcntl.h>
#include <memory>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace bc::launcher {
namespace {
struct CurlRuntime {
    CurlRuntime() { require(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "Cannot initialize HTTPS"); }
    ~CurlRuntime() { curl_global_cleanup(); }
};
void init_curl() { static CurlRuntime runtime; }
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
std::string url_part(CURLU* url, CURLUPart part) {
    char* text = nullptr;
    const auto status = curl_url_get(url, part, &text, 0);
    std::unique_ptr<char, decltype(&curl_free)> owner(text, curl_free);
    return status == CURLUE_OK ? std::string(text) : "";
}
struct Sink {
    int fd;
    uint64_t count = 0, maximum;
    static size_t write(char* data, size_t size, size_t count, void* pointer) noexcept {
        auto& sink = *static_cast<Sink*>(pointer);
        if (size && count > SIZE_MAX / size) return 0;
        const auto bytes = size * count;
        if (bytes > sink.maximum - sink.count) return 0;
        size_t offset = 0;
        while (offset < bytes) {
            auto written = ::write(sink.fd, data + offset, bytes - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) return 0;
            offset += written;
        }
        sink.count += bytes;
        return bytes;
    }
};
}
void allowed_url(const std::string& value) {
    init_curl();
    Url url(curl_url(), curl_url_cleanup);
    require(url && value.starts_with("https://") && value.find_first_of("\r\n\\") == std::string::npos &&
            curl_url_set(url.get(), CURLUPART_URL, value.c_str(), CURLU_DISALLOW_USER) == CURLUE_OK, "Invalid HTTPS URL");
    static const std::set<std::string> hosts{"api.github.com", "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"};
    const auto port = url_part(url.get(), CURLUPART_PORT);
    require(url_part(url.get(), CURLUPART_SCHEME) == "https" && hosts.contains(url_part(url.get(), CURLUPART_HOST)) &&
            (port.empty() || port == "443") && url_part(url.get(), CURLUPART_FRAGMENT).empty(), "Untrusted download URL");
}
void download(const std::string& input, const fs::path& path, int timeout, uint64_t maximum) {
    require(timeout >= 1 && timeout <= 120 && maximum <= max_archive, "Invalid download bounds");
    allowed_url(input); plain(path);
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    require(fd >= 0, "Cannot create download file");
    struct Close { int fd; ~Close() { close(fd); } } close_fd{fd};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
    std::string url = input;
    try {
        for (int redirect = 0; redirect <= 5; ++redirect) {
            allowed_url(url);
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            require(remaining > 0, "Download deadline exceeded");
            require(ftruncate(fd, 0) == 0 && lseek(fd, 0, SEEK_SET) == 0, "Cannot reset download");
            Sink sink{fd, 0, maximum};
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
            require(bool(curl), "Cannot create HTTPS request");
            auto option = [&](CURLoption key, auto value) { require(curl_easy_setopt(curl.get(), key, value) == CURLE_OK, "Cannot configure HTTPS request"); };
            option(CURLOPT_URL, url.c_str());
            option(CURLOPT_PROTOCOLS_STR, "https");
            // Redirects are checked before each request, never followed implicitly.
            option(CURLOPT_FOLLOWLOCATION, 0L);
            option(CURLOPT_SSL_VERIFYPEER, 1L); option(CURLOPT_SSL_VERIFYHOST, 2L);
            option(CURLOPT_NOSIGNAL, 1L); option(CURLOPT_TIMEOUT_MS, long(remaining));
            option(CURLOPT_CONNECTTIMEOUT_MS, long(std::min<int64_t>(remaining, 10000)));
            option(CURLOPT_MAXFILESIZE_LARGE, curl_off_t(maximum));
            option(CURLOPT_USERAGENT, "BriefcaseNative-native-launcher");
            option(CURLOPT_WRITEFUNCTION, &Sink::write); option(CURLOPT_WRITEDATA, &sink);
            const auto result = curl_easy_perform(curl.get());
            require(result == CURLE_OK, "HTTPS transfer failed: " + std::string(curl_easy_strerror(result)));
            long status = 0; curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
            if (status == 200) { require(fsync(fd) == 0, "Cannot sync download"); return; }
            require(status == 301 || status == 302 || status == 303 || status == 307 || status == 308, "GitHub request failed (HTTP " + std::to_string(status) + ")");
            char* next = nullptr; curl_easy_getinfo(curl.get(), CURLINFO_REDIRECT_URL, &next);
            require(next != nullptr, "Missing redirect URL"); url = next;
        }
        throw std::runtime_error("Too many HTTPS redirects");
    } catch (...) { unlink(path.c_str()); throw; }
}
void extract(const fs::path& file, const fs::path& stage, bool mod_package) {
    // Never delegate paths or filesystem creation to the archive library.
    require(fs::file_size(plain(file)) <= max_archive, "Archive exceeds limit");
    require(!fs::exists(plain(stage)) || fs::is_empty(stage), "Extraction directory must be empty");
    fs::create_directories(stage);
    std::unique_ptr<archive, decltype(&archive_read_free)> input(archive_read_new(), archive_read_free);
    require(bool(input), "Cannot create ZIP reader");
    require(archive_read_support_format_zip(input.get()) == ARCHIVE_OK, "Cannot enable ZIP reader");
    int fd = open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    require(fd >= 0, "Cannot open archive");
    struct Close { int fd; ~Close() { close(fd); } } close_fd{fd};
    struct stat info{};
    require(fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1, "Unsafe archive file");
    require(archive_read_open_fd(input.get(), fd, 65536) == ARCHIVE_OK, "Cannot read ZIP");
    std::set<std::string> seen;
    uint64_t total = 0;
    archive_entry* entry = nullptr;
    int status;
    while ((status = archive_read_next_header(input.get(), &entry)) == ARCHIVE_OK) {
        const char* raw_name = archive_entry_pathname(entry);
        require(raw_name != nullptr, "Missing ZIP path");
        const std::string name(raw_name);
        const auto target = package_path(stage, name);
        const bool mod_path = name == "ModPackage.json" ||
            std::regex_match(name, std::regex("Briefcase/Mods/[a-z0-9]+([.-][a-z0-9]+)*/[A-Za-z0-9_./-]+"));
        require((mod_package ? mod_path : managed(name)) && seen.insert(name).second && seen.size() <= 4096,
                "Unexpected or repeated archive file");
        require(archive_entry_filetype(entry) == AE_IFREG && !archive_entry_symlink(entry) && !archive_entry_hardlink(entry) &&
                !archive_entry_is_encrypted(entry) && !(archive_entry_perm(entry) & 07000), "ZIP links, special files or privileged modes refused");
        const auto size = archive_entry_size(entry);
        require(archive_entry_size_is_set(entry) && size >= 0 && uint64_t(size) <= max_archive, "Invalid ZIP file size");
        total += size; require(total <= 1024ull * 1024 * 1024, "Expanded ZIP exceeds limit");
        fs::create_directories(target.parent_path()); plain(target);
        int out = open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        require(out >= 0, "Cannot create ZIP output");
        Close close_out{out}; Sink sink{out, 0, uint64_t(size)};
        std::array<char, 65536> buffer{};
        while (true) {
            auto count = archive_read_data(input.get(), buffer.data(), buffer.size());
            require(count >= 0, "Damaged ZIP data");
            if (!count) break;
            require(Sink::write(buffer.data(), 1, count, &sink) == size_t(count), "ZIP write or size failure");
        }
        require(sink.count == uint64_t(size), "Truncated ZIP file");
    }
    require(status == ARCHIVE_EOF && !seen.empty(), "Invalid ZIP structure");
    require(archive_read_close(input.get()) == ARCHIVE_OK, "ZIP close failed");
}
}
