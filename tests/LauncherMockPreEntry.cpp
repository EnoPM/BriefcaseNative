#include <Windows.h>

extern "C" __declspec(dllexport) unsigned __cdecl BriefcasePreEntry() noexcept {
    return SetEnvironmentVariableW(L"BC_TEST_PREENTRY", L"1") ? 0 : 1;
}
