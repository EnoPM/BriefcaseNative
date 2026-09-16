#include "../../runtime/Briefcase.NativeHost/GameProfile.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(slashes * (character == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (character == L'\"') result += L'\\';
        result += character;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid Unicode path");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}
fs::path executable_path() {
    std::vector<wchar_t> buffer(32768);
    const auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count == buffer.size()) throw std::runtime_error("Cannot locate the client launcher");
    return fs::absolute(fs::path(std::wstring(buffer.data(), count))).lexically_normal();
}
void validate_plain_path(const fs::path& path) {
    fs::path prefix;
    for (const auto& part : fs::absolute(path).lexically_normal()) {
        if (part == L"..") throw std::runtime_error("Parent traversal refused");
        prefix /= part;
        const auto attributes = GetFileAttributesW(prefix.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Reparse point refused in client path");
    }
}
void require_file(const fs::path& path, const char* message) {
    validate_plain_path(path);
    if (!fs::is_regular_file(path)) throw std::runtime_error(message);
}
void validate_game(const fs::path& path) {
    require_file(path, "Deceive Inc. client executable is missing");
    Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot inspect the client executable");
    IMAGE_DOS_HEADER dos{}; DWORD read{};
    if (!ReadFile(file.value, &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
        throw std::runtime_error("Invalid client executable header");
    LARGE_INTEGER offset{}; offset.QuadPart = dos.e_lfanew;
    if (!SetFilePointerEx(file.value, offset, nullptr, FILE_BEGIN)) throw std::runtime_error("Invalid client executable offset");
    IMAGE_NT_HEADERS64 nt{};
    if (!ReadFile(file.value, &nt, sizeof(nt), &read, nullptr) || read != sizeof(nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        !bc::game_profile(path.filename().wstring(), nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage) ||
        bc::game_profile(path.filename().wstring(), nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage)->environment != bc::Environment::client)
        throw std::runtime_error("This Deceive Inc. client build is not supported by Briefcase");
}
bool same_path(const fs::path& left, const fs::path& right) {
    const auto a = fs::absolute(left).lexically_normal().wstring();
    const auto b = fs::absolute(right).lexically_normal().wstring();
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
bool client_running(const fs::path& executable) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot inspect running processes");
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot.value, &entry)) return false;
    do {
        if (_wcsicmp(entry.szExeFile, executable.filename().c_str()) != 0) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        if (!process.value) continue;
        wchar_t path[32768]{}; DWORD size = 32768;
        if (QueryFullProcessImageNameW(process.value, 0, path, &size) && same_path(path, executable)) return true;
    } while (Process32NextW(snapshot.value, &entry));
    return false;
}
void append_error(const fs::path& root, const std::string& message) noexcept {
    try {
        fs::create_directories(root / L"Briefcase/Logs");
        std::ofstream(root / L"Briefcase/Logs/client-launcher-error.log", std::ios::app) << message << '\n';
    } catch (...) {}
}
int launch(const fs::path& root, const std::vector<std::wstring>& supplied, bool validate_only) {
    if (root.filename() != L"Win64" || root.parent_path().filename() != L"Binaries" ||
        root.parent_path().parent_path().filename() != L"DeceiveInc")
        throw std::runtime_error("Briefcase.ClientLauncher.exe must be installed in DeceiveInc/Binaries/Win64");
    validate_plain_path(root);
    const auto game = root / L"DeceiveInc-Win64-Shipping.exe";
    validate_game(game);
    require_file(root / L"version.dll", "Briefcase client proxy is missing");
    require_file(root / L"Briefcase/Core/Briefcase.NativeHost.dll", "Briefcase client runtime is missing");
    require_file(root / L"Briefcase/Core/Client/Briefcase.Client.Rendering.dll", "Briefcase client renderer is missing");
    if (validate_only) return 0;
    if (client_running(game)) throw std::runtime_error("This Deceive Inc. client is already running");
    const auto game_log = root / L"Briefcase/Logs/DeceiveInc-client.log";
    fs::create_directories(game_log.parent_path());
    std::vector<std::wstring> arguments{L"-ABSLOG=" + game_log.wstring()};
    arguments.insert(arguments.end(), supplied.begin(), supplied.end());
    auto command = quote(game.wstring());
    for (const auto& argument : arguments) {
        if (argument.find_first_of(L"\r\n") != std::wstring::npos) throw std::runtime_error("Invalid client argument");
        command += L" " + quote(argument);
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(),
                        &startup, &process))
        throw std::runtime_error("Cannot start Deceive Inc. client: " + std::to_string(GetLastError()));
    Handle child{process.hProcess}, thread{process.hThread};
    const auto pid = process.dwProcessId;
    nlohmann::json record{{"executable", utf8(game.wstring())}, {"workingDirectory", utf8(root.wstring())},
                          {"pid", pid}, {"startedAt", std::time(nullptr)}, {"arguments", nlohmann::json::array()}};
    for (const auto& argument : arguments) record["arguments"].push_back(utf8(argument));
    std::ofstream(root / L"Briefcase/Logs/last-launch.json", std::ios::trunc) << record.dump(2) << '\n';
    if (WaitForSingleObject(child.value, 3000) == WAIT_OBJECT_0) {
        DWORD code{}; GetExitCodeProcess(child.value, &code);
        throw std::runtime_error("Deceive Inc. exited during Briefcase bootstrap with code " + std::to_string(code));
    }
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    fs::path root;
    try {
        root = executable_path().parent_path();
        bool validate_only = false;
        std::vector<std::wstring> arguments;
        for (int index = 1; index < argc; ++index) {
            const std::wstring value(argv[index]);
            if (value == L"--validate-only") validate_only = true;
            else arguments.push_back(value);
        }
        return launch(root, arguments, validate_only);
    } catch (const std::exception& error) {
        if (!root.empty()) append_error(root, error.what());
        MessageBoxA(nullptr, error.what(), "Briefcase client launcher", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        return 78;
    } catch (...) {
        if (!root.empty()) append_error(root, "Unexpected client launcher failure");
        return 78;
    }
}
