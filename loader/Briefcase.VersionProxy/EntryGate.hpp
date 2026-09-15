#pragma once
#include <Windows.h>
#include <cstdint>
// One-shot entry gate. Only the initial EXE entry is redirected; never an Unreal callback.
namespace entry_gate {
inline uint8_t *address{};
inline uint8_t original{};
inline void *handler{};
inline void (*callback)(){};
inline DWORD thread{};
inline void finish() {
    // Executed outside the exception dispatcher and loader lock, before EXE CRT/game startup.
    if (handler) {
        RemoveVectoredExceptionHandler(handler);
        handler = nullptr;
    }
    callback();
    reinterpret_cast<void (*)()>(address)();
}
inline LONG WINAPI exception(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT ||
        e->ExceptionRecord->ExceptionAddress != address || GetCurrentThreadId() != thread)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD previous{}, unused{};
    if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &previous))
        TerminateProcess(GetCurrentProcess(), 119);
    *address = original;
    const bool ok = FlushInstructionCache(GetCurrentProcess(), address, 1) != FALSE;
    const bool restored = VirtualProtect(address, 1, previous, &unused) != FALSE;
    if (!ok || !restored)
        TerminateProcess(GetCurrentProcess(), 119);
    e->ContextRecord->Rip = reinterpret_cast<DWORD64>(&finish);
    return EXCEPTION_CONTINUE_EXECUTION;
}
inline bool install(void (*prepare)()) {
    auto base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    const auto rva = nt->OptionalHeader.AddressOfEntryPoint;
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || !rva ||
        rva >= nt->OptionalHeader.SizeOfImage)
        return false;
    address = base + rva;
    original = *address;
    thread = GetCurrentThreadId();
    callback = prepare;
    if (original == 0xcc)
        return false;
    handler = AddVectoredExceptionHandler(1, exception);
    if (!handler)
        return false;
    DWORD previous{}, unused{};
    if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &previous)) {
        RemoveVectoredExceptionHandler(handler);
        handler = nullptr;
        return false;
    }
    *address = 0xcc;
    const bool ok = FlushInstructionCache(GetCurrentProcess(), address, 1) != FALSE;
    const bool restored = VirtualProtect(address, 1, previous, &unused) != FALSE;
    if (!ok || !restored)
        TerminateProcess(GetCurrentProcess(), 119);
    return true;
}
} // namespace entry_gate
