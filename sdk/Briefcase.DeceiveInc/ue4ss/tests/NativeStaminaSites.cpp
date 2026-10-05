#include "../src/NativeStaminaSites.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uintptr_t base = 0x140001000;
constexpr std::size_t thunk = 0x20;
constexpr std::size_t target = 0x180;

void require(bool condition) {
    if (!condition) throw std::runtime_error("NativeStamina thunk discovery contract failed");
}

void reject(const std::function<void()> &call) {
    bool failed{};
    try { call(); } catch (const std::runtime_error &) { failed = true; }
    require(failed);
}

void call(std::vector<std::uint8_t> &code, std::size_t at, std::size_t destination) {
    code[at] = 0xe8;
    const auto displacement = static_cast<std::int32_t>(destination) -
                              static_cast<std::int32_t>(at + 5);
    std::memcpy(code.data() + at + 1, &displacement, sizeof(displacement));
}

void jump(std::vector<std::uint8_t> &code, std::size_t at, std::size_t destination) {
    code[at] = 0xe9;
    const auto displacement = static_cast<std::int32_t>(destination) -
                              static_cast<std::int32_t>(at + 5);
    std::memcpy(code.data() + at + 1, &displacement, sizeof(displacement));
}

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> code(512, 0x90);
    // Unreal's reflected thunk first materializes a float parameter on its
    // stack, then passes that value in XMM1 and the saved Spy in RCX.
    constexpr std::array<std::uint8_t, 6> delta{0xf3, 0x0f, 0x10, 0x4c, 0x24, 0x38};
    std::memcpy(code.data() + thunk, delta.data(), delta.size());
    code[thunk + 6] = 0x48;
    code[thunk + 7] = 0x8b;
    code[thunk + 8] = 0xcf; // mov rcx,rdi
    call(code, thunk + 9, target);
    code[thunk + 14] = 0xc3;
    code[target] = 0x53;
    code[target + 1] = 0xc3;
    return code;
}

std::vector<std::uint8_t> reset_fixture() {
    std::vector<std::uint8_t> code(512, 0x90);
    constexpr std::array<std::uint8_t, 4> parameter{0x48, 0x8b, 0x42, 0x20};
    std::memcpy(code.data() + thunk, parameter.data(), parameter.size());
    jump(code, thunk + 4, target);
    code[target] = 0x53;
    code[target + 1] = 0xc3;
    return code;
}

std::uintptr_t resolve_shipping_image(const char *path, const char *thunk_rva_text,
                                      bool reset = false) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open Shipping executable");
    const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(input), {}};
    if (image.size() < sizeof(IMAGE_DOS_HEADER))
        throw std::runtime_error("Shipping executable is truncated");
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<std::size_t>(dos->e_lfanew) > image.size() - sizeof(IMAGE_NT_HEADERS64))
        throw std::runtime_error("Shipping executable has an invalid DOS header");
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(image.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.NumberOfSections > 32)
        throw std::runtime_error("Shipping executable has an invalid PE header");
    const auto thunk_rva = std::stoull(thunk_rva_text, nullptr, 0);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto &section = IMAGE_FIRST_SECTION(nt)[i];
        if (std::memcmp(section.Name, ".text", 5) != 0) continue;
        if (section.PointerToRawData > image.size() ||
            section.SizeOfRawData > image.size() - section.PointerToRawData)
            throw std::runtime_error("Shipping code section is outside file");
        const auto text = std::span{image.data() + section.PointerToRawData,
                                    static_cast<std::size_t>(section.SizeOfRawData)};
        const auto address = nt->OptionalHeader.ImageBase + section.VirtualAddress;
        const auto thunk_address = nt->OptionalHeader.ImageBase + thunk_rva;
        const auto resolved = reset
            ? briefcase::deceive::detail::find_native_reset_stamina(text, address, thunk_address)
            : briefcase::deceive::detail::find_native_reduce_stamina(text, address, thunk_address);
        return resolved - nt->OptionalHeader.ImageBase;
    }
    throw std::runtime_error("Shipping code section is missing");
}
} // namespace

int main(int argc, char **argv) {
    try {
        auto code = fixture();
        const auto find = [&](const auto &bytes) {
            return briefcase::deceive::detail::find_native_reduce_stamina(
                bytes, base, base + thunk);
        };
        require(find(code) == base + target);

        // A game update may move both methods, but the reflected thunk still
        // identifies the direct native call without a saved RVA or signature.
        auto moved = fixture();
        call(moved, thunk + 9, target + 0x40);
        require(find(moved) == base + target + 0x40);

        auto missing_delta = fixture();
        missing_delta[thunk] = 0x90;
        reject([&] { (void)find(missing_delta); });

        auto indirect_call = fixture();
        indirect_call[thunk + 9] = 0xff;
        reject([&] { (void)find(indirect_call); });

        auto outside = fixture();
        call(outside, thunk + 9, 0x400);
        reject([&] { (void)find(outside); });

        auto ambiguous = fixture();
        ambiguous[thunk + 14] = 0x90;
        ambiguous[thunk + 15] = 0x48;
        ambiguous[thunk + 16] = 0x8b;
        ambiguous[thunk + 17] = 0xcf;
        call(ambiguous, thunk + 18, target + 0x20);
        ambiguous[thunk + 23] = 0xc3;
        reject([&] { (void)find(ambiguous); });

        reject([&] {
            (void)briefcase::deceive::detail::find_native_reduce_stamina(
                code, base, base + code.size());
        });
        auto reset = reset_fixture();
        const auto find_reset = [&](const auto &bytes) {
            return briefcase::deceive::detail::find_native_reset_stamina(
                bytes, base, base + thunk);
        };
        require(find_reset(reset) == base + target);
        auto moved_reset = reset_fixture();
        jump(moved_reset, thunk + 4, target + 0x40);
        require(find_reset(moved_reset) == base + target + 0x40);
        auto indirect_reset = reset_fixture();
        indirect_reset[thunk + 4] = 0xff;
        reject([&] { (void)find_reset(indirect_reset); });
        auto outside_reset = reset_fixture();
        jump(outside_reset, thunk + 4, 0x400);
        reject([&] { (void)find_reset(outside_reset); });
        auto early_return = reset_fixture();
        early_return[thunk + 4] = 0xc3;
        reject([&] { (void)find_reset(early_return); });
        auto call_before_jump = reset_fixture();
        call(call_before_jump, thunk + 4, target + 0x20);
        jump(call_before_jump, thunk + 9, target);
        reject([&] { (void)find_reset(call_before_jump); });
        std::cout << "PASS native stamina thunk discovery and fail-closed contracts\n";
        if (argc == 4) {
            const auto site = resolve_shipping_image(argv[1], argv[2]);
            std::cout << "Shipping native ReduceStamina RVA: 0x" << std::hex << site << '\n';
            const auto reset_site = resolve_shipping_image(argv[1], argv[3], true);
            std::cout << "Shipping native ResetStaminaToMax RVA: 0x" << std::hex << reset_site << '\n';
        } else if (argc == 3) {
            const auto site = resolve_shipping_image(argv[1], argv[2]);
            std::cout << "Shipping native ReduceStamina RVA: 0x" << std::hex << site << '\n';
        } else if (argc != 1) {
            throw std::runtime_error("Usage: test [Shipping.exe reduce-thunk-rva [reset-thunk-rva]]");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
