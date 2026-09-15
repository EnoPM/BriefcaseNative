#include "NativeContract.hpp"
#include "PropertyPath.hpp"
#include <MinHook.h>
#include <Windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
static int checks;
static void check(bool b) {
    ++checks;
    if (!b)
        throw std::runtime_error("Native contract " + std::to_string(checks));
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (...) {
        rejected = true;
    }
    check(rejected);
}
struct State {
    volatile float value = 0;
    volatile int count = 0;
};
__declspec(noinline) static void vanilla(State *self, float delta) {
    self->value = self->value + delta;
    self->count = self->count + 1;
}
static void (*original)(State *, float);
static int before_count, after_count;
static void observer(State *self, float delta) {
    ++before_count;
    original(self, delta);
    ++after_count;
}
int main() {
    try {
        std::array<uint8_t, 4096> image{};
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(image.data());
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 128;
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(image.data() + 128);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt->FileHeader.NumberOfSections = 1;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        auto *section = IMAGE_FIRST_SECTION(nt);
        section->VirtualAddress = 1024;
        section->Misc.VirtualSize = 2048;
        section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
        std::array<uint8_t, 16> exec{0x90, 0xe8};
        int32_t displacement = 2048 - (1024 + 6);
        std::memcpy(exec.data() + 2, &displacement, 4);
        std::array<uint8_t, 32> body{};
        body.fill(0x90);
        std::memcpy(image.data() + 1024, exec.data(), exec.size());
        std::memcpy(image.data() + 2048, body.data(), body.size());
        BcNativeSite site{sizeof(site),          1024,       1, uint32_t(exec.size()), exec.data(), 2048,
                          uint32_t(body.size()), body.data()};
        bc::validate_native_site(image.data(), uint32_t(image.size()), site);
        check(true);
        for (auto field : {&site.size, &site.exec_rva, &site.branch_offset, &site.exec_size, &site.target_rva,
                           &site.target_size}) {
            auto save = *field;
            *field = 0;
            rejects([&] { bc::validate_native_site(image.data(), uint32_t(image.size()), site); });
            *field = save;
        }
        site.branch_offset = 2;
        rejects([&] { bc::validate_native_site(image.data(), uint32_t(image.size()), site); });
        site.branch_offset = 1;
        image[2048] = 0xcc;
        rejects([&] { bc::validate_native_site(image.data(), uint32_t(image.size()), site); });
        image[2048] = 0x90;
        section->Characteristics = 0;
        rejects([&] { bc::validate_native_site(image.data(), uint32_t(image.size()), site); });
        section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
        for (auto path : {"HeatState.HeatCount", "GamePhase", "A.b_c.D2"})
            check(!bc::property_path(path).empty());
        for (auto path : {"", "x.", ".x", "x..y", "x[0]", "x y", "a.b.c.d.e.f.g.h.i"})
            rejects([&] { bc::property_path(path); });
        check(MH_Initialize() == MH_OK);
        std::array<uint8_t, 32> original_bytes{};
        std::memcpy(original_bytes.data(), reinterpret_cast<void *>(&vanilla), 32);
        check(MH_CreateHook(reinterpret_cast<void *>(&vanilla), reinterpret_cast<void *>(&observer),
                            reinterpret_cast<void **>(&original)) == MH_OK);
        check(MH_EnableHook(reinterpret_cast<void *>(&vanilla)) == MH_OK);
        State state;
        auto volatile caller = &vanilla;
        caller(&state, 2.5f);
        check(state.value == 2.5f && state.count == 1 && before_count == 1 && after_count == 1);
        caller(&state, -1.f);
        check(state.value == 1.5f && state.count == 2 && before_count == 2 && after_count == 2);
        check(MH_DisableHook(reinterpret_cast<void *>(&vanilla)) == MH_OK);
        check(std::memcmp(original_bytes.data(), reinterpret_cast<void *>(&vanilla), 32) == 0);
        caller(&state, 3.f);
        check(state.value == 4.5f && state.count == 3 && before_count == 2 && after_count == 2);
        check(MH_Uninitialize() == MH_OK);
        std::cout << "PASS " << checks << " native hook and property path contracts\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
