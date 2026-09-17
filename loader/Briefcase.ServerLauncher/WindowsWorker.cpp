#include "Update.hpp"
#include "LaunchWindows.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <chrono>
#include <fstream>
#include <regex>
#include <thread>

using namespace bc::launcher;
namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    require(size > 0, "Invalid Unicode path");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    require(size > 0, "Invalid UTF-8 text");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
void append_log(const fs::path& root, const std::string& message) {
    std::error_code ignored; fs::create_directories(root / "Briefcase/Logs", ignored);
    std::ofstream(root / "Briefcase/Logs/launcher.log", std::ios::app) << message << '\n';
}
void wait_parent(DWORD pid) {
    if (!pid) return;
    Handle process{OpenProcess(SYNCHRONIZE, FALSE, pid)};
    if (!process.value) {
        require(GetLastError() == ERROR_INVALID_PARAMETER, "Cannot wait for launcher process");
        return;
    }
    require(WaitForSingleObject(process.value, 60000) == WAIT_OBJECT_0, "Launcher process did not exit");
}
bool same_path(const fs::path& left, const fs::path& right) {
    const auto a = fs::absolute(left).lexically_normal().wstring();
    const auto b = fs::absolute(right).lexically_normal().wstring();
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
bool server_running(const fs::path& executable) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    require(snapshot.value != INVALID_HANDLE_VALUE, "Cannot inspect running processes");
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
std::pair<int, int> ports(const fs::path& root) {
    int game = 50000, query = 50001;
    const auto ini = root.parent_path().parent_path() / "Saved/Config/WindowsServer/TripwireServer.ini";
    if (fs::exists(plain(ini))) {
        const auto text = read(ini, 262144);
        std::smatch match;
        if (std::regex_search(text, match, std::regex("(?:^|\\r?\\n)GamePort=([0-9]+)(?:\\r?$|\\r?\\n)"))) game = std::stoi(match[1]);
        if (std::regex_search(text, match, std::regex("(?:^|\\r?\\n)QueryPort=([0-9]+)(?:\\r?$|\\r?\\n)"))) query = std::stoi(match[1]);
    }
    require(game >= 1 && game <= 65535 && query >= 1 && query <= 65535 && game != query, "Invalid configured server ports");
    return {game, query};
}
std::wstring command_arguments(const std::vector<std::wstring>& supplied, int game_port, int query_port) {
    std::wstring result = L"-unattended -NoSplash -NOCONSOLE -nullrhi -nosound -Port=" + std::to_wstring(game_port) +
                          L" -QueryPort=" + std::to_wstring(query_port);
    for (const auto& argument : supplied) {
        std::wstring lower = argument; for (auto& value : lower) value = towlower(value);
        require(lower != L"-log" && lower != L"-console" && !lower.starts_with(L"-servergui"),
                "Graphical console arguments are not supported");
        if (lower == L"-unattended" || lower == L"-nosplash" || lower == L"-noconsole" ||
            lower == L"-nullrhi" || lower == L"-nosound") continue;
        result += L" " + briefcase::launcher::quote(argument);
    }
    return result;
}
std::string stamp() {
    SYSTEMTIME value{}; GetSystemTime(&value);
    char text[64]{};
    std::snprintf(text, sizeof(text), "%04u%02u%02u-%02u%02u%02u-%03u", value.wYear, value.wMonth,
                  value.wDay, value.wHour, value.wMinute, value.wSecond, value.wMilliseconds);
    return text;
}
int run(const fs::path& input_root, DWORD parent, DWORD wait_for, const std::string& restart_id,
        const std::vector<std::wstring>& arguments) {
    wait_parent(parent);
    wait_parent(wait_for);
    const auto root = plain(input_root);
    require(root.filename() == L"Win64" && root.parent_path().filename() == L"Binaries" &&
            root.parent_path().parent_path().filename() == L"DeceiveInc", "Invalid Win64 server directory");
    const auto game = plain(root / game_name);
    require(fs::is_regular_file(game), "Dedicated server executable is missing");
    const auto launch_path = package_path(root, "Briefcase/launch.json");
    if (!fs::exists(launch_path))
        write_json(launch_path, {{"serverWin64", utf8(root.wstring())}});
    const auto launch = document(launch_path);
    require(launch.contains("serverWin64") && launch.at("serverWin64").is_string() &&
            same_path(root, fs::path(wide(launch.at("serverWin64").get<std::string>()))),
            "Launcher is not authorized for this Win64 directory");
    DWORD previous_pid{};
    if (!restart_id.empty()) {
        const auto pending = document(package_path(root, "Briefcase/Admin/restart-result.json"));
        require(pending.value("requestId", "") == restart_id &&
                    pending.value("state", "") == "starting" &&
                    pending.contains("previousPid") && pending.at("previousPid").is_number_unsigned() &&
                    pending.at("previousPid").get<uint64_t>() <= MAXDWORD,
                "Invalid pending restart result");
        previous_pid = pending.at("previousPid").get<DWORD>();
    }
    fs::create_directories(package_path(root, "Briefcase/Updates"));
    Handle lock{CreateFileW(package_path(root, "Briefcase/Updates/launch.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    require(lock.value != INVALID_HANDLE_VALUE, "Another update or launch is already in progress");
    require(!server_running(game), "This dedicated server is already running");
    auto logger = [&](const std::string& value) { append_log(root, value); };
    Updater updater(root);
    const auto framework = updater.update(logger);
    const auto mods = updater.update_mods(logger);
    write_json(package_path(root, "Briefcase/Updates/last-result.json"),
               {{"framework", framework}, {"mods", mods}, {"checkedAt", std::time(nullptr)}});
    updater.cleanup(logger);
    const auto [game_port, query_port] = ports(root);
    const auto native_arguments = command_arguments(arguments, game_port, query_port);
    const auto pid = briefcase::launcher::start(game,
        package_path(root, "Briefcase/Core/Briefcase.ServerBootstrap.dll"), native_arguments);
    Json record{{"executable", utf8(game.wstring())}, {"workingDirectory", utf8(root.wstring())},
                {"pid", pid}, {"update", framework}, {"modUpdates", mods},
                {"startedAt", std::time(nullptr)}, {"arguments", Json::array()}};
    for (const auto& argument : arguments) record["arguments"].push_back(utf8(argument));
    write_json(package_path(root, "Briefcase/Logs/launch-" + stamp() + ".json"), record);
    if (!restart_id.empty()) {
        write_json(package_path(root, "Briefcase/Admin/restart-result.json"),
                   {{"requestId", restart_id}, {"previousPid", previous_pid}, {"pid", pid}, {"state", "started"},
                    {"workingDirectory", utf8(root.wstring())}});
    }
    std::this_thread::sleep_for(std::chrono::seconds(3));
    Handle child{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    require(child.value && WaitForSingleObject(child.value, 0) == WAIT_TIMEOUT,
            "Server exited during startup; check the game logs");
    append_log(root, "Server started pid=" + std::to_string(pid));
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    fs::path root;
    std::string restart_id;
    try {
        DWORD parent{}, wait_for{}; std::vector<std::wstring> arguments;
        bool game_arguments = false;
        for (int index = 1; index < argc; ++index) {
            const std::wstring argument(argv[index]);
            if (!game_arguments && argument == L"--") { game_arguments = true; continue; }
            if (!game_arguments && (argument == L"--root" || argument == L"--parent" ||
                                    argument == L"--wait-parent" || argument == L"--restart")) {
                require(++index < argc, "Missing worker option value");
                if (argument == L"--root") root = argv[index];
                else if (argument == L"--parent") parent = std::stoul(argv[index]);
                else if (argument == L"--wait-parent") wait_for = std::stoul(argv[index]);
                else restart_id = utf8(argv[index]);
                continue;
            }
            require(game_arguments, "Unknown worker option"); arguments.push_back(argument);
        }
        require(!root.empty() && parent, "Missing worker launch context");
        if (!restart_id.empty()) require(std::regex_match(restart_id, std::regex("[a-f0-9]{32}")), "Invalid restart identifier");
        return run(root, parent, wait_for, restart_id, arguments);
    } catch (const std::exception& error) {
        if (!root.empty()) {
            append_log(root, std::string("Native update coordinator failed: ") + error.what());
            if (!restart_id.empty()) try {
                write_json(package_path(root, "Briefcase/Admin/restart-result.json"),
                           {{"requestId", restart_id}, {"state", "error"}, {"message", error.what()}});
            } catch (...) {}
        }
        return 78;
    } catch (...) { return 78; }
}
