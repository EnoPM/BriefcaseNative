#include "../loader/Briefcase.ServerLauncher/LaunchWindows.hpp"
#include <fstream>
// This initializer executes before main. Preparation must have happened before EXE CRT startup.
static bool prepared_before_crt = [] {
    wchar_t value[16]{};
    return GetEnvironmentVariableW(L"BC_TEST_PREPARED", value, 16) != 0;
}();
int wmain() {
    using namespace briefcase::launcher;
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
    std::filesystem::path self(path);
    if (self.filename() == L"DeceiveIncServer-Win64-Shipping.exe") {
        if (!prepared_before_crt || std::filesystem::current_path() != self.parent_path()) return 11;
        if (GetModuleHandleW(L"version.dll") && std::filesystem::exists(self.parent_path()/L"version.dll")) return 12;
        std::ofstream(self.parent_path()/L"entry-verified.txt") << "prepared before CRT; cwd is Win64";
        wchar_t lifetime[16]{};
        auto delay = GetEnvironmentVariableW(L"BC_TEST_SERVER_LIFETIME", lifetime, 16) ? wcstoul(lifetime,nullptr,10) : 500;
        Sleep(delay); return 0;
    }
    try {
        auto stage = self.parent_path()/L"launcher-fixture"/std::to_wstring(GetTickCount64())/L"DeceiveInc/Binaries/Win64";
        std::filesystem::create_directories(stage/L"Briefcase/Core");
        auto exe=stage/L"DeceiveIncServer-Win64-Shipping.exe";
        auto dll=stage/L"Briefcase/Core/Briefcase.ServerBootstrap.dll";
        std::filesystem::copy_file(self,exe);
        std::filesystem::copy_file(self.parent_path()/L"Briefcase.ServerBootstrap.dll",dll);
        std::filesystem::copy_file(self.parent_path()/L"Briefcase.LauncherMockHost.dll",stage/L"Briefcase/Core/Briefcase.NativeHost.dll");
        auto pid=start(exe,dll,L"-NOCONSOLE -nullrhi",5000);
        Handle child(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
        if (!child.value || WaitForSingleObject(child.value,5000)!=WAIT_OBJECT_0) return 2;
        DWORD code=1; GetExitCodeProcess(child.value,&code);
        if(code || !std::filesystem::exists(stage/L"entry-verified.txt")) return 3;
        SetEnvironmentVariableW(L"BC_TEST_PREPARE_FAIL",L"1");
        bool rejected=false;
        try { start(exe,dll,L"",5000); } catch(...) { rejected=true; }
        SetEnvironmentVariableW(L"BC_TEST_PREPARE_FAIL",nullptr);
        if(!rejected) return 4;
        std::filesystem::remove(stage/L"Briefcase/Core/Briefcase.NativeHost.dll");
        rejected=false;
        try { start(exe,dll,L"",5000); } catch(...) { rejected=true; }
        if(!rejected) return 5;
        if(quote(L"a b\\")!=L"\"a b\\\\\"" || quote(L"a\"b")!=L"\"a\\\"b\"") return 6;
        puts("PASS native injection before EXE CRT, primary thread, Win64 cwd, missing host, failed prepare and argument quoting.");
        return 0;
    } catch(const std::exception& e) { fprintf(stderr,"%s\n",e.what()); return 1; }
}
