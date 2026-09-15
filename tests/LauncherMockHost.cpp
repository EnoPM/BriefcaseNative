#include <Windows.h>
#include <cstdint>
extern "C" __declspec(dllexport) uint32_t __cdecl BriefcasePrepare(uint32_t thread) noexcept {
    if (thread != GetCurrentThreadId()) return 1;
    wchar_t failure[2]{};
    if (GetEnvironmentVariableW(L"BC_TEST_PREPARE_FAIL", failure, 2)) return 1;
    SetEnvironmentVariableW(L"BC_TEST_PREPARED", L"yes");
    return 0;
}
extern "C" __declspec(dllexport) uint32_t __cdecl BriefcaseRun() noexcept { return 0; }
