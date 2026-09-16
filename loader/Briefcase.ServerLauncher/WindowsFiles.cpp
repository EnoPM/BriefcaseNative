#include "Update.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <regex>
#include <set>
#include <vector>

namespace bc::launcher {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::string hex(const unsigned char* bytes, size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}
void regular(HANDLE handle, uint64_t limit) {
    FILE_STANDARD_INFO info{};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    require(handle != INVALID_HANDLE_VALUE && GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info)) &&
            GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) &&
            !info.Directory && info.NumberOfLinks == 1 && !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            info.EndOfFile.QuadPart >= 0 && static_cast<uint64_t>(info.EndOfFile.QuadPart) <= limit,
            "Unsafe or oversized file");
}
Handle open_read(const fs::path& path, uint64_t limit) {
    Handle handle{CreateFileW(plain(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    require(handle.value != INVALID_HANDLE_VALUE, "Cannot open file");
    regular(handle.value, limit); return handle;
}
void write_all(HANDLE file, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const auto request = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
        DWORD written{};
        require(WriteFile(file, data.data() + offset, request, &written, nullptr) && written == request,
                "File write failed");
        offset += written;
    }
}
}
fs::path plain(const fs::path& input) {
    const auto result = fs::absolute(input).lexically_normal();
    fs::path prefix;
    for (const auto& part : result) {
        require(part != L"..", "Parent traversal refused");
        prefix /= part;
        const auto attributes = GetFileAttributesW(prefix.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            require(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND,
                    "Cannot inspect path");
        } else require(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "Reparse point refused");
    }
    return result;
}
fs::path package_path(const fs::path& root, const std::string& name) {
    require(!name.empty() && name.front() != '/' && name.back() != '/' &&
            std::regex_match(name, std::regex("[A-Za-z0-9_./-]+")), "Invalid package path");
    size_t start = 0;
    do {
        const auto end = name.find('/', start);
        const auto part = name.substr(start, end == std::string::npos ? end : end - start);
        require(!part.empty() && part != "." && part != ".." && part.back() != '.' &&
                !std::regex_match(part, std::regex("(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\\..*)?", std::regex::icase)),
                "Unsafe package path");
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    const auto base = plain(root);
    const auto result = plain(base / fs::path(name));
    auto base_text = base.wstring(); if (!base_text.ends_with(L'\\')) base_text += L'\\';
    const auto text = result.wstring();
    require(text.size() > base_text.size() && _wcsnicmp(text.c_str(), base_text.c_str(), base_text.size()) == 0,
            "Package path escaped installation");
    return result;
}
bool managed(const std::string& name, bool legacy) {
    static const std::set<std::string> current{
        "Briefcase.ServerLauncher.exe", "Package.json", "Briefcase/Core/Briefcase.NativeHost.dll",
        "Briefcase/Core/Briefcase.ServerBootstrap.dll", "Briefcase/Core/Tools/Briefcase.AdminSetup.exe",
        "Briefcase/Core/Tools/Briefcase.ServerRestart.exe", "Briefcase/Core/Tools/Briefcase.ServerUpdater.exe",
        "Briefcase/Core/Updater/build.json", "Briefcase/Core/Updater/updater.example.json"};
    static const std::set<std::string> retired{
        "version.dll", "StartBriefcaseNativeServer.ps1", "Briefcase/Updater/Updater.ps1",
        "Briefcase/Updater/Restart-Server.ps1", "Briefcase/Updater/Launch-Server.ps1",
        "Briefcase/Runtime/Briefcase.NativeHost.dll", "Briefcase/Runtime/Briefcase.ServerBootstrap.dll",
        "Briefcase/Tools/Briefcase.AdminSetup.exe", "Briefcase/Tools/Briefcase.ServerRestart.exe",
        "Briefcase/Tools/Briefcase.ServerUpdater.exe", "Briefcase/Updater/build.json",
        "Briefcase/Updater/updater.example.json"};
    return current.contains(name) || (legacy && retired.contains(name)) ||
           std::regex_match(name, std::regex("Briefcase/Core/(Docs|Licenses|Localization)/[A-Za-z0-9_./-]+\\.(json|txt|md)")) ||
           (legacy && std::regex_match(name, std::regex("Briefcase/(Docs|Licenses|Localization)/[A-Za-z0-9_./-]+\\.(json|txt|md)")));
}
std::string read(const fs::path& path, uint64_t limit) {
    auto file = open_read(path, limit);
    LARGE_INTEGER size{}; require(GetFileSizeEx(file.value, &size), "Cannot read file size");
    std::string result(static_cast<size_t>(size.QuadPart), '\0');
    size_t offset = 0;
    while (offset < result.size()) {
        const auto request = static_cast<DWORD>(std::min<size_t>(result.size() - offset, 1u << 20));
        DWORD count{}; require(ReadFile(file.value, result.data() + offset, request, &count, nullptr) && count == request,
                               "Cannot read file");
        offset += count;
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
    const auto data = read(path, UINT64_MAX);
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
            "SHA-256 initialization failed");
    struct Algorithm { BCRYPT_ALG_HANDLE value; ~Algorithm() { BCryptCloseAlgorithmProvider(value, 0); } } close_algorithm{algorithm};
    DWORD object_size{}, actual{};
    require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &actual, 0) == 0,
            "SHA-256 object query failed");
    std::vector<unsigned char> object(object_size); std::array<unsigned char, 32> output{};
    require(BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) == 0,
            "SHA-256 creation failed");
    struct Hash { BCRYPT_HASH_HANDLE value; ~Hash() { BCryptDestroyHash(value); } } close_hash{hash};
    require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), 0) == 0 &&
            BCryptFinishHash(hash, output.data(), static_cast<ULONG>(output.size()), 0) == 0, "SHA-256 failed");
    return hex(output.data(), output.size());
}
std::string identifier() {
    std::array<unsigned char, 16> bytes{};
    require(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0,
            "Random source failed");
    return hex(bytes.data(), bytes.size());
}
void atomic(const fs::path& path, const std::string& data, unsigned) {
    plain(path); fs::create_directories(path.parent_path()); plain(path.parent_path());
    if (fs::exists(path)) { auto old = open_read(path, max_archive); }
    const auto temporary = path.parent_path() / (L"." + path.filename().wstring() + L"." + fs::path(identifier()).wstring());
    Handle file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    require(file.value != INVALID_HANDLE_VALUE, "Cannot create atomic file");
    try {
        write_all(file.value, data); require(FlushFileBuffers(file.value), "Cannot sync file");
        CloseHandle(file.value); file.value = INVALID_HANDLE_VALUE;
        plain(path);
        require(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                "Cannot replace file");
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}
unsigned file_mode(const fs::path& path) { auto file = open_read(path, max_archive); return 0644; }
void write_json(const fs::path& path, const Json& value) { atomic(path, value.dump(2) + "\n"); }
void remove_file(const fs::path& path) {
    if (!fs::exists(plain(path))) return;
    auto file = open_read(path, max_archive); CloseHandle(file.value); file.value = INVALID_HANDLE_VALUE;
    require(DeleteFileW(path.c_str()), "Cannot remove obsolete file");
}
}
