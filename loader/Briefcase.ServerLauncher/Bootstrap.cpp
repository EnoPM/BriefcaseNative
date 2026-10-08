#include "../Briefcase.VersionProxy/EntryGate.hpp"
#include "../Briefcase.VersionProxy/PreEntryMods.hpp"
#include <detours.h>
#include <filesystem>
static DWORD WINAPI run(void* value) {
    return reinterpret_cast<uint32_t(__cdecl*)()>(value)();
}
static void prepare() noexcept {
    try {
        wchar_t exe[32768]{};
        if (!GetModuleFileNameW(nullptr, exe, 32768)) ExitProcess(119);
        const auto root = std::filesystem::path(exe).parent_path();
        auto host = LoadLibraryExW((root / L"Briefcase/Core/Briefcase.NativeHost.dll").c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!host) ExitProcess(119);
        auto init = reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(GetProcAddress(host, "BriefcasePrepare"));
        auto loop = GetProcAddress(host, "BriefcaseRun");
        if (!init || !loop || init(GetCurrentThreadId())) ExitProcess(119);
        if (!briefcase::loader::load_preentry_mods(root)) ExitProcess(119);
        // During the UE4SS migration the launcher remains the single injection
        // point. A server package can opt in by installing ue4ss/UE4SS.dll;
        // no proxy DLL beside the game executable is required.
        const auto ue4ss_path = root / L"ue4ss/UE4SS.dll";
        if (std::filesystem::is_regular_file(ue4ss_path) &&
            !LoadLibraryExW(ue4ss_path.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32))
            ExitProcess(119);
        auto worker = CreateThread(nullptr, 0, run, reinterpret_cast<void*>(loop), 0, nullptr);
        if (!worker) ExitProcess(119);
        CloseHandle(worker);
        auto name = L"Local\\BriefcaseNative.Prepared." + std::to_wstring(GetCurrentProcessId());
        auto ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
        if (!ready) ExitProcess(119);
        const auto ok = SetEvent(ready); CloseHandle(ready);
        if (!ok) ExitProcess(119);
    } catch (...) { ExitProcess(119); }
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (DetourIsHelperProcess()) return TRUE;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        if (!DetourRestoreAfterWith() || !entry_gate::install(prepare)) return FALSE;
    }
    return TRUE;
}
