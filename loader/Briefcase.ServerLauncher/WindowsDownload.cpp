#include "Update.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <miniz.h>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <regex>
#include <set>
#include <vector>

namespace bc::launcher {
namespace {
struct Internet {
    HINTERNET value{};
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
std::wstring wide(const std::string& value) {
    require(!value.empty(), "Empty URL");
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    require(count > 0, "Invalid UTF-8 URL");
    std::wstring result(count, L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count) == count,
            "Invalid UTF-8 URL");
    return result;
}
struct ParsedUrl { std::wstring host, target; INTERNET_PORT port{}; };
ParsedUrl parse_url(const std::string& value) {
    require(value.starts_with("https://") && value.find_first_of("\r\n\\") == std::string::npos, "Invalid HTTPS URL");
    const auto input = wide(value);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = DWORD(-1); parts.dwUrlPathLength = DWORD(-1); parts.dwExtraInfoLength = DWORD(-1);
    require(WinHttpCrackUrl(input.c_str(), static_cast<DWORD>(input.size()), ICU_REJECT_USERPWD, &parts) &&
            parts.nScheme == INTERNET_SCHEME_HTTPS && parts.nPort == INTERNET_DEFAULT_HTTPS_PORT,
            "Invalid HTTPS URL");
    ParsedUrl result;
    result.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    result.target.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) result.target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    result.port = parts.nPort;
    std::wstring host = result.host; std::ranges::transform(host, host.begin(), towlower);
    static const std::set<std::wstring> hosts{L"api.github.com", L"github.com", L"release-assets.githubusercontent.com", L"objects.githubusercontent.com"};
    require(hosts.contains(host) && !result.target.empty(), "Untrusted download URL");
    return result;
}
std::string header(HINTERNET request, DWORD query) {
    DWORD bytes{};
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes >= sizeof(wchar_t), "Missing HTTP header");
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    require(WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes, WINHTTP_NO_HEADER_INDEX),
            "Cannot read HTTP header");
    value.resize(wcslen(value.c_str()));
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    require(count > 0, "Invalid HTTP header");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}
void allowed_url(const std::string& value) { (void)parse_url(value); }
void download(const std::string& input, const fs::path& path, int timeout, uint64_t maximum) {
    require(timeout >= 1 && timeout <= 120 && maximum <= max_archive, "Invalid download bounds");
    allowed_url(input); plain(path); require(!fs::exists(path), "Download target already exists");
    Internet session{WinHttpOpen(L"BriefcaseNative-native-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    require(session.value, "Cannot initialize HTTPS");
    const auto milliseconds = timeout * 1000;
    require(WinHttpSetTimeouts(session.value, std::min(milliseconds, 10000), milliseconds, milliseconds, milliseconds),
            "Cannot configure HTTPS timeout");
    std::string url = input;
    for (int redirects = 0; redirects <= 5; ++redirects) {
        const auto parsed = parse_url(url);
        Internet connection{WinHttpConnect(session.value, parsed.host.c_str(), parsed.port, 0)};
        require(connection.value, "Cannot connect to HTTPS host");
        Internet request{WinHttpOpenRequest(connection.value, L"GET", parsed.target.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
        require(request.value, "Cannot create HTTPS request");
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        require(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)) &&
                WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request.value, nullptr), "HTTPS transfer failed");
        DWORD status{}, size = sizeof(status);
        require(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX),
                "Cannot read HTTP status");
        if (status == 200) {
            std::string data;
            while (true) {
                DWORD available{}; require(WinHttpQueryDataAvailable(request.value, &available), "HTTPS read failed");
                if (!available) break;
                require(available <= maximum - data.size(), "Download exceeds size limit");
                const auto offset = data.size(); data.resize(offset + available);
                DWORD received{}; require(WinHttpReadData(request.value, data.data() + offset, available, &received) && received,
                                          "HTTPS read failed");
                data.resize(offset + received);
            }
            require(!data.empty(), "Empty HTTPS response"); atomic(path, data); return;
        }
        require(status == 301 || status == 302 || status == 303 || status == 307 || status == 308,
                "GitHub request failed (HTTP " + std::to_string(status) + ")");
        url = header(request.value, WINHTTP_QUERY_LOCATION);
        allowed_url(url);
    }
    throw std::runtime_error("Too many HTTPS redirects");
}
void extract(const fs::path& file, const fs::path& stage, bool mod_package) {
    const auto archive_data = read(plain(file), max_archive);
    require(!fs::exists(plain(stage)) || fs::is_empty(stage), "Extraction directory must be empty");
    fs::create_directories(stage); plain(stage);
    mz_zip_archive archive{};
    require(mz_zip_reader_init_mem(&archive, archive_data.data(), archive_data.size(), 0), "Cannot read ZIP");
    struct Archive { mz_zip_archive* value; ~Archive() { mz_zip_reader_end(value); } } close{&archive};
    require(mz_zip_validate_archive(&archive, 0), "Invalid ZIP structure");
    const auto count = mz_zip_reader_get_num_files(&archive);
    require(count > 0 && count <= 4096, "Invalid ZIP entry count");
    std::set<std::string> seen;
    uint64_t total{};
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat info{};
        require(mz_zip_reader_file_stat(&archive, index, &info) && info.m_filename[0] && !info.m_is_encrypted && info.m_is_supported,
                "Unsupported ZIP entry");
        std::string raw(info.m_filename);
        const bool directory = info.m_is_directory != 0;
        while (directory && !raw.empty() && raw.back() == '/') raw.pop_back();
        require(!raw.empty() && raw.find('\\') == raw.npos, "Invalid ZIP path");
        const auto target = package_path(stage, raw);
        const bool mod_path = raw == "ModPackage.json" ||
            std::regex_match(raw, std::regex("Briefcase/Mods/[a-z0-9]+([.-][a-z0-9]+)*/[A-Za-z0-9_./-]+"));
        require((mod_package ? mod_path : managed(raw)) && seen.insert(lowercase(raw)).second,
                "Unexpected or repeated archive file");
        const auto unix_mode = (info.m_external_attr >> 16) & 0177777;
        require(!(unix_mode & 07000) && (directory || (unix_mode & 0170000) == 0 ||
                (unix_mode & 0170000) == 0100000),
                "ZIP links, special files or privileged modes refused");
        if (directory) { require(info.m_uncomp_size == 0, "Invalid ZIP directory"); fs::create_directories(target); continue; }
        require(info.m_uncomp_size <= max_archive && total <= 1024ull * 1024 * 1024 - info.m_uncomp_size,
                "Expanded ZIP exceeds limit");
        total += info.m_uncomp_size;
        size_t size{}; void* bytes = mz_zip_reader_extract_to_heap(&archive, index, &size, 0);
        std::unique_ptr<void, decltype(&mz_free)> owner(bytes, mz_free);
        require(bytes && size == info.m_uncomp_size, "Damaged or truncated ZIP data");
        atomic(target, std::string(static_cast<const char*>(bytes), size), 0644);
    }
}
}
