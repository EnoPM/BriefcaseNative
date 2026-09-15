#include "../loader/Briefcase.VersionProxy/EntryGate.hpp"
// The same entry gate runs from TLS, before the executable entry, outside any game.
static volatile LONG prepared = 0;
static void prepare() {
    InterlockedIncrement(&prepared);
}
static void NTAPI tls(void *, DWORD reason, void *) {
    if (reason == DLL_PROCESS_ATTACH && !entry_gate::install(prepare))
        TerminateProcess(GetCurrentProcess(), 20);
}
#pragma const_seg(".CRT$XLB")
extern "C" const PIMAGE_TLS_CALLBACK entry_gate_tls = tls;
#pragma const_seg()
#pragma comment(linker, "/INCLUDE:entry_gate_tls")
#pragma comment(linker, "/INCLUDE:_tls_used")
int main() {
    if (prepared != 1 || *entry_gate::address != entry_gate::original || entry_gate::handler)
        return 21;
    // No lasting exception handler or patched byte remains.
    return 0;
}
